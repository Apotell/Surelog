/*
 Copyright 2019 Alain Dargelas

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

/*
 * File:   CompileExpression.cpp
 * Author: alain
 *
 * Created on May 14, 2019, 8:03 PM
 */

#include "Surelog/CommandLine/CommandLineParser.h"
#include "Surelog/Common/FileSystem.h"
#include "Surelog/Common/NodeId.h"
#include "Surelog/Common/Session.h"
#include "Surelog/Design/DataType.h"
#include "Surelog/Design/DummyType.h"
#include "Surelog/Design/Enum.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/Design/ModuleDefinition.h"
#include "Surelog/Design/ModuleInstance.h"
#include "Surelog/Design/ParamAssign.h"
#include "Surelog/Design/Parameter.h"
#include "Surelog/Design/Signal.h"
#include "Surelog/Design/SimpleType.h"
#include "Surelog/Design/Struct.h"
#include "Surelog/Design/Union.h"
#include "Surelog/Design/ValuedComponentI.h"
#include "Surelog/DesignCompile/CompileDesign.h"
#include "Surelog/DesignCompile/CompileHelper.h"
#include "Surelog/DesignCompile/UhdmWriter.h"
#include "Surelog/ErrorReporting/Error.h"
#include "Surelog/ErrorReporting/ErrorDefinition.h"
#include "Surelog/ErrorReporting/Location.h"
#include "Surelog/Library/Library.h"
#include "Surelog/Package/Package.h"
#include "Surelog/SourceCompile/Compiler.h"
#include "Surelog/SourceCompile/SymbolTable.h"
#include "Surelog/SourceCompile/VObjectTypes.h"
#include "Surelog/Testbench/ClassDefinition.h"
#include "Surelog/Testbench/TypeDef.h"
#include "Surelog/Testbench/Variable.h"
#include "Surelog/Utils/StringUtils.h"

// UHDM
#include <uhdm/BaseClass.h>
#include <uhdm/ElaboratorListener.h>
#include <uhdm/Utils.h>
#include <uhdm/clone_tree.h>
#include <uhdm/expr.h>
#include <uhdm/uhdm.h>
#include <uhdm/uhdm_types.h>
#include <uhdm/vpi_user.h>

#include <cstdint>
#include <stack>
#include <string>
#include <vector>

namespace SURELOG {

using namespace uhdm;  // NOLINT (using a bunch of them)

uhdm::Any* CompileHelper::compileVariable(
    DesignComponent* component, const FileContent* fC, NodeId declarationId,
    NodeId nameId, NodeId unpackedDimId, CompileDesign* compileDesign,
    uhdm::Any* pstmt, SURELOG::ValuedComponentI* instance, bool muteErrors) {
  uhdm::Serializer& s = compileDesign->getSerializer();
  Design* const design = compileDesign->getCompiler()->getDesign();
  uhdm::Any* result = nullptr;
  NodeId variable = declarationId;
  VObjectType the_type = fC->Type(variable);
  if (the_type == VObjectType::paData_type ||
      the_type == VObjectType::paPs_or_hierarchical_identifier) {
    variable = fC->Child(variable);
    the_type = fC->Type(variable);
    if (the_type == VObjectType::VIRTUAL) {
      variable = fC->Sibling(variable);
      the_type = fC->Type(variable);
    }
  } else if (the_type == VObjectType::paImplicit_class_handle) {
    NodeId Handle = fC->Child(variable);
    if (fC->Type(Handle) == VObjectType::paThis_keyword) {
      variable = fC->Sibling(variable);
      the_type = fC->Type(variable);
    }
  } else if (the_type == VObjectType::_INVALID_) {
    return nullptr;
  }
  if (the_type == VObjectType::paComplex_func_call) {
    variable = fC->Child(variable);
    the_type = fC->Type(variable);
  }
  NodeId Packed_dimension = variable;
  if (fC->Type(Packed_dimension) != VObjectType::paPacked_dimension)
    Packed_dimension = fC->Sibling(variable);
  if (!Packed_dimension) {
    // Implicit return value:
    // function [1:0] fct();
    if (fC->Type(variable) == VObjectType::paConstant_range) {
      Packed_dimension = variable;
    }
  }

  if (fC->Type(variable) == VObjectType::STRING_CONST &&
      fC->Type(Packed_dimension) == VObjectType::STRING_CONST) {
    uhdm::HierPath* path = s.make<uhdm::HierPath>();
    path->setParent(pstmt);
    uhdm::AnyCollection* elems = path->getPathElems(true);
    std::string fullName(fC->SymName(variable));
    uhdm::RefObj* obj = s.make<uhdm::RefObj>();
    obj->setName(fullName);
    obj->setParent(path);
    elems->emplace_back(obj);
    fC->populateCoreMembers(variable, variable, obj);
    path->setFile(obj->getFile());
    while (fC->Type(Packed_dimension) == VObjectType::STRING_CONST) {
      uhdm::RefObj* obj = s.make<uhdm::RefObj>();
      const std::string_view name = fC->SymName(Packed_dimension);
      fullName.append(".").append(name);
      obj->setName(name);
      obj->setParent(path);
      fC->populateCoreMembers(Packed_dimension, Packed_dimension, obj);
      elems->emplace_back(obj);
      Packed_dimension = fC->Sibling(Packed_dimension);
    }
    path->setFullName(fullName);
    if (!elems->empty()) {
      path->setStartLine(elems->front()->getStartLine());
      path->setStartColumn(elems->front()->getStartColumn());
      path->setEndLine(elems->back()->getEndLine());
      path->setEndColumn(elems->back()->getEndColumn());
    }
    return path;
  }

  uhdm::Typespec* ts = nullptr;
  VObjectType decl_type = fC->Type(declarationId);
  if ((decl_type != VObjectType::paPs_or_hierarchical_identifier) &&
      (decl_type != VObjectType::paImplicit_class_handle) &&
      (decl_type != VObjectType::STRING_CONST)) {
    ts = compileTypespec(component, fC, declarationId, unpackedDimId,
                         compileDesign, pstmt, instance, true);
  }

  switch (the_type) {
    case VObjectType::STRING_CONST:
    case VObjectType::paChandle_type: {
      const std::string_view typeName = fC->SymName(variable);

      if (const DataType* dt = component->getDataType(design, typeName)) {
        if (uhdm::Typespec* tps = dt->getActualTypespec()) {
          result = compileVariable(component, fC, compileDesign, nameId,
                                   the_type, declarationId, tps, pstmt);
        }
      }
      if (result == nullptr) {
        std::string typespecName(typeName);
        ClassDefinition* cl = design->getClassDefinition(typeName);
        if (cl == nullptr) {
          std::string scopedName = StrCat(component->getName(), "::", typeName);
          if ((cl = design->getClassDefinition(scopedName))) {
            typespecName = scopedName;
          }
        }
        if (cl == nullptr) {
          if (const DesignComponent* p =
                  valuedcomponenti_cast<const DesignComponent*>(
                      component->getParentScope())) {
            std::string scopedName = StrCat(p->getName(), "::", typeName);
            if ((cl = design->getClassDefinition(scopedName))) {
              typespecName = scopedName;
            }
          }
        }
        if (cl) {
          ts = cl->getUhdmTypespecModel();
          uhdm::Variable* var = s.make<uhdm::Variable>();
          uhdm::RefTypespec* tpsRef = s.make<uhdm::RefTypespec>();
          tpsRef->setName(typespecName);
          tpsRef->setParent(var);
          tpsRef->setActual(ts);
          var->setTypespec(tpsRef);
          var->setName(fC->SymName(nameId));
          fC->populateCoreMembers(variable, variable, tpsRef);
          fC->populateCoreMembers(nameId, nameId, var);
          result = var;
        }
      }
      if ((result == nullptr) && (the_type == VObjectType::STRING_CONST) &&
          (ts != nullptr) &&
          (ts->getUhdmType() == uhdm::UhdmType::ClassTypespec)) {
        uhdm::Variable* var = s.make<uhdm::Variable>();
        uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
        tsRef->setParent(var);
        tsRef->setActual(ts);
        tsRef->setName(ts->getName());
        var->setTypespec(tsRef);
        var->setName(fC->SymName(nameId));
        fC->populateCoreMembers(nameId, nameId, var);
        fC->populateCoreMembers(declarationId, declarationId, tsRef);
        result = var;
      }
      if (result == nullptr) {
        if (the_type == VObjectType::paChandle_type) {
          uhdm::Variable* var = s.make<uhdm::Variable>();
          var->setName(fC->SymName(nameId));
          fC->populateCoreMembers(nameId, nameId, var);
          if (ts) {
            uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
            tsRef->setParent(var);
            tsRef->setActual(ts);
            tsRef->setName(ts->getName());
            fC->populateCoreMembers(declarationId, declarationId, tsRef);
            var->setTypespec(tsRef);
          }
          result = var;
        } else {
          if ((ts != nullptr) &&
              (ts->getUhdmType() == uhdm::UhdmType::ArrayTypespec)) {
            uhdm::Variable* const av = s.make<uhdm::Variable>();
            av->setName(fC->SymName(nameId));
            result = av;
          } else {
            uhdm::RefObj* ro = s.make<uhdm::RefObj>();
            ro->setName(fC->SymName(nameId));
            result = ro;
          }
          fC->populateCoreMembers(nameId, nameId, result);

          if ((result != nullptr) &&
              (result->getUhdmType() != UhdmType::RefObj)) {
            if (ts == nullptr) {
              uhdm::UnsupportedTypespec* ut =
                  s.make<uhdm::UnsupportedTypespec>();
              ut->setName(typeName);
              ut->setParent(pstmt);
              fC->populateCoreMembers(declarationId, declarationId, ut);
              ts = ut;
            }

            uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
            tsRef->setParent(result);
            tsRef->setActual(ts);
            setRefTypespecName(tsRef, ts, typeName);
            fC->populateCoreMembers(declarationId, declarationId, tsRef);
            any_cast<uhdm::SimpleExpr>(result)->setTypespec(tsRef);
          }
        }
      }
      break;
    }
    case VObjectType::paClass_scope: {
      NodeId class_type = fC->Child(variable);
      NodeId class_name = fC->Child(class_type);
      const std::string_view packageName = fC->SymName(class_name);
      Design* const design = compileDesign->getCompiler()->getDesign();
      NodeId symb_id = fC->Sibling(variable);
      const std::string_view typeName = fC->SymName(symb_id);
      Package* pack = design->getPackage(packageName);
      uhdm::Any* var = nullptr;
      if (pack) {
        const DataType* dtype = pack->getDataType(design, typeName);
        while (dtype) {
          if (uhdm::Typespec* const tps = dtype->getTypespec()) {
            var = compileVariable(component, fC, compileDesign, nameId,
                                  the_type, declarationId, tps, pstmt);
            break;
          }
          dtype = dtype->getDefinition();
        }
      }
      if (var == nullptr) {
        ClassDefinition* cl = design->getClassDefinition(packageName);
        if (cl == nullptr) {
          cl = design->getClassDefinition(
              StrCat(component->getName(), "::", packageName));
        }
        if (cl == nullptr) {
          if (const DesignComponent* p =
                  valuedcomponenti_cast<const DesignComponent*>(
                      component->getParentScope())) {
            cl = design->getClassDefinition(
                StrCat(p->getName(), "::", packageName));
          }
        }
        if (cl) {
          const DataType* dtype = cl->getDataType(design, typeName);
          while (dtype) {
            if (uhdm::Typespec* const tps = dtype->getTypespec()) {
              var = compileVariable(component, fC, compileDesign, nameId,
                                    the_type, declarationId, tps, pstmt);
              break;
            }
            dtype = dtype->getDefinition();
          }
        }
      }

      if (var == nullptr) {
        const std::string completeName = StrCat(packageName, "::", typeName);

        uhdm::Variable* const v = s.make<uhdm::Variable>();
        v->setName(completeName);

        uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
        tsRef->setName(completeName);
        tsRef->setParent(v);
        tsRef->setActual(ts);
        fC->populateCoreMembers(declarationId, declarationId, tsRef);
        v->setTypespec(tsRef);
        var = v;
      }
      result = var;
      break;
    }
    default: {
      result = compileVariable(component, fC, compileDesign, nameId, the_type,
                               declarationId, ts, pstmt);
      if (pstmt) result->setParent(pstmt, true);
      // Implicit type
      if ((result == nullptr) && declarationId) {
        uhdm::Variable* var = s.make<uhdm::Variable>();
        var->setParent(pstmt);

        if (ts == nullptr) {
          uhdm::LogicTypespec* lts = s.make<uhdm::LogicTypespec>();
          lts->setParent(var);
          fC->populateCoreMembers(declarationId, declarationId, lts);

          // 6.8 Variable declarations
          // The byte, shortint, int, integer, and longint types are signed
          // types by default. Other net and variable types can be explicitly
          // declared as signed.
          const NodeId signId = fC->Sibling(variable);
          if (signId && (fC->Type(signId) == VObjectType::paSigning_Signed)) {
            lts->setSigned(true);
          }

          ts = lts;
        }

        uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
        tsRef->setParent(var);
        tsRef->setActual(ts);
        fC->populateCoreMembers(declarationId, declarationId, tsRef);
        var->setTypespec(tsRef);

        result = var;
      }
      break;
    }
  }
  if (result != nullptr) {
    result->setParent(pstmt);
    fC->populateCoreMembers(nameId, nameId, result);
  }
  return result;
}

uhdm::Any* CompileHelper::compileVariable(
    DesignComponent* component, const FileContent* fC,
    CompileDesign* compileDesign, NodeId nameId, VObjectType subnettype,
    NodeId typespecId, uhdm::Typespec* tps, uhdm::Any* pscope) {
  uhdm::Serializer& s = compileDesign->getSerializer();
  Design* const design = compileDesign->getCompiler()->getDesign();
  if (pscope == nullptr) pscope = component->getUhdmModel();
  if (pscope == nullptr) pscope = design->getUhdmDesign();

  bool isInModule = false;
  const Any* p = pscope;
  while ((p != nullptr) && !isInModule) {
    if (p->getUhdmType() == uhdm::UhdmType::Module) {
      isInModule = true;
    }
    p = p->getParent();
  }

  const std::string_view signame = fC->SymName(nameId);
  uhdm::Any* obj = nullptr;
  uhdm::RefTypespec* tpsRef = nullptr;

  if (tps) {
    uhdm::UhdmType tpstype = tps->getUhdmType();
    if (tpstype == uhdm::UhdmType::TypedefTypespec) {
      uhdm::Typespec* tmptps = tps;
      while ((tmptps != nullptr) &&
             (tmptps->getUhdmType() == uhdm::UhdmType::TypedefTypespec)) {
        tmptps = static_cast<uhdm::TypedefTypespec*>(tmptps)
                     ->getTypedefAlias()
                     ->getActual();
      }
      if (tmptps) tpstype = tmptps->getUhdmType();
    }

    switch (tpstype) {
      case uhdm::UhdmType::ArrayTypespec:
      case uhdm::UhdmType::StructTypespec:
      case uhdm::UhdmType::LogicTypespec:
      case uhdm::UhdmType::EnumTypespec:
      case uhdm::UhdmType::IntegerTypespec:
      case uhdm::UhdmType::TimeTypespec: {
        if (isInModule) {
          obj = s.make<uhdm::Net>();
        } else {
          obj = s.make<uhdm::Variable>();
        }
      } break;
      case uhdm::UhdmType::BitTypespec:
      case uhdm::UhdmType::ByteTypespec:
      case uhdm::UhdmType::RealTypespec:
      case uhdm::UhdmType::ShortRealTypespec:
      case uhdm::UhdmType::IntTypespec:
      case uhdm::UhdmType::UnionTypespec:
      case uhdm::UhdmType::ClassTypespec:
      case uhdm::UhdmType::InterfaceTypespec:
      case uhdm::UhdmType::LongIntTypespec:
      case uhdm::UhdmType::ShortIntTypespec:
      case uhdm::UhdmType::StringTypespec:
      case uhdm::UhdmType::VoidTypespec: {
        obj = s.make<uhdm::Variable>();
      } break;
      default:
        break;
    }
  }

  if (obj == nullptr) {
    if ((subnettype == VObjectType::paIntegerAtomType_Shortint) ||
        (subnettype == VObjectType::paIntegerAtomType_Int) ||
        (subnettype == VObjectType::paIntegerAtomType_Integer) ||
        (subnettype == VObjectType::paIntegerAtomType_LongInt) ||
        (subnettype == VObjectType::paIntegerAtomType_Time) ||
        (subnettype == VObjectType::paIntVec_TypeBit) ||
        (subnettype == VObjectType::paIntegerAtomType_Byte) ||
        (subnettype == VObjectType::paNonIntType_ShortReal) ||
        (subnettype == VObjectType::paNonIntType_Real) ||
        (subnettype == VObjectType::paNonIntType_RealTime) ||
        (subnettype == VObjectType::paString_type) ||
        (subnettype == VObjectType::paChandle_type) ||
        (subnettype == VObjectType::paIntVec_TypeLogic)) {
      obj = s.make<uhdm::Variable>();
    } else if (subnettype == VObjectType::paEvent_type) {
      uhdm::NamedEvent* event = s.make<uhdm::NamedEvent>();
      event->setName(signame);
      return event;
    }
  }
  // default type (fallback)
  if (obj == nullptr) {
    if (isInModule) {
      obj = s.make<uhdm::Net>();
    } else {
      obj = s.make<uhdm::Variable>();
    }
  }

  if (uhdm::Variable* const var = any_cast<uhdm::Variable>(obj)) {
    var->setName(signame);
  } else if (Nets* const nets = any_cast<Nets>(obj)) {
    nets->setName(signame);
  }

  if (tps != nullptr) {
    if (uhdm::Expr* const e = any_cast<uhdm::Expr>(obj)) {
      std::string_view name = tps->getName();
      if (name.empty() || (name == SymbolFactory::getBadSymbol())) {
        name = fC->SymName(typespecId);
      }
      tpsRef = s.make<uhdm::RefTypespec>();
      tpsRef->setParent(e);
      tpsRef->setActual(tps);
      e->setTypespec(tpsRef);
      setRefTypespecName(tpsRef, tps, name);
      fC->populateCoreMembers(typespecId, typespecId, tpsRef);
    }
  }

  fC->populateCoreMembers(nameId, nameId, obj);
  obj->setParent(pscope);
  return obj;
}

uhdm::Any* CompileHelper::compileSignals(DesignComponent* component,
                                         CompileDesign* compileDesign,
                                         Signal* sig, uhdm::Typespec* tps) {
  // uhdm::Serializer& s = compileDesign->getSerializer();
  const DataType* dtype = sig->getDataType();
  VObjectType subnettype = sig->getType();
  NodeId signalId = sig->getNameId();
  NodeId typespecId = sig->getTypespecId() ? sig->getTypespecId()
                                           : sig->getInterfaceTypeNameId();
  const FileContent* const fC = sig->getFileContent();
  Design* const design = compileDesign->getCompiler()->getDesign();
  uhdm::Any* pscope = sig->uhdmScopeModel();
  if (pscope == nullptr) pscope = component->getUhdmModel();
  if (pscope == nullptr) pscope = design->getUhdmDesign();

  uhdm::Typespec* updatedts = nullptr;
  uhdm::Any* obj = nullptr;
  if (tps == nullptr) {
    while (dtype) {
      if (const TypeDef* tdef = datatype_cast<TypeDef>(dtype)) {
        if (tdef->getTypespec()) {
          tps = tdef->getTypespec();
          while ((tps != nullptr) &&
                 (tps->getUhdmType() == uhdm::UhdmType::TypedefTypespec)) {
            tps = static_cast<uhdm::TypedefTypespec*>(tps)
                      ->getTypedefAlias()
                      ->getActual();
          }
          break;
        }
      } else if (const Enum* en = datatype_cast<Enum>(dtype)) {
        tps = en->getTypespec();
        break;
      } else if (const Struct* st = datatype_cast<Struct>(dtype)) {
        tps = st->getTypespec();
        break;
      } else if (const Union* st = datatype_cast<Union>(dtype)) {
        tps = st->getTypespec();
        break;
      } else if (const DummyType* un = datatype_cast<DummyType>(dtype)) {
        tps = un->getTypespec();
        if (tps == nullptr) {
          tps = compileTypespec(component, un->getFileContent(),
                                un->getNodeId(), InvalidNodeId, compileDesign,
                                nullptr, nullptr, true);
          ((DummyType*)un)->setTypespec(updatedts);
        }
        break;
      } else if (const SimpleType* sit = datatype_cast<SimpleType>(dtype)) {
        tps = sit->getTypespec();
        tps =
            elabTypespec(component, updatedts, compileDesign, nullptr, nullptr);
        break;
      } else if (/*const ClassDefinition* cl = */ datatype_cast<
                 ClassDefinition>(dtype)) {
      } else if (Parameter* sit =
                     const_cast<Parameter*>(datatype_cast<Parameter>(dtype))) {
        tps = compileTypeParameter(component, compileDesign, sit);
      }
      dtype = dtype->getDefinition();
    }
  }
  if (tps != nullptr) uhdm::setSigned(tps, sig->isSigned());
  obj = compileVariable(component, fC, compileDesign, signalId, subnettype,
                        typespecId, tps, pscope);
  if (SimpleExpr* const se = any_cast<SimpleExpr>(obj)) {
    if (uhdm::AttributeCollection* const attributes = sig->attributes()) {
      se->setAttributes(attributes);
      for (uhdm::Attribute* attribute : *attributes) attribute->setParent(obj);
    }
  }

  if (Nets* const nets = any_cast<Nets>(obj)) {
    nets->setNetType(UhdmWriter::getVpiNetType(sig->getType()));
  }

  if (uhdm::Variable* const var = any_cast<uhdm::Variable>(obj)) {
    var->setConstantVariable(sig->isConst());
    var->setIsRandomized(sig->isRand() || sig->isRandc());
    var->setAutomatic(!sig->isStatic());
    if (sig->isRand()) {
      var->setRandType(vpiRand);
    } else if (sig->isRandc()) {
      var->setRandType(vpiRandC);
    } else {
      var->setRandType(vpiNotRand);
    }
    if (sig->isProtected()) {
      var->setVisibility(vpiProtectedVis);
    } else if (sig->isLocal()) {
      var->setVisibility(vpiLocalVis);
    } else {
      var->setVisibility(vpiPublicVis);
    }
  }
  return obj;
}

uhdm::Any* CompileHelper::compileSignals(DesignComponent* component,
                                         CompileDesign* compileDesign,
                                         Signal* sig) {
  const FileContent* const fC = sig->getFileContent();
  Design* const design = compileDesign->getCompiler()->getDesign();
  uhdm::Any* pscope = component->getUhdmModel();
  if (pscope == nullptr) pscope = design->getUhdmDesign();

  uhdm::Any* uhdmScope = sig->uhdmScopeModel();
  NodeId typeSpecId = sig->getTypespecId() ? sig->getTypespecId()
                                           : sig->getInterfaceTypeNameId();
  if (uhdm::Typespec* tps = compileTypespec(
          component, fC, typeSpecId, sig->getUnpackedDimension(), compileDesign,
          uhdmScope, nullptr, false)) {
    return compileSignals(component, compileDesign, sig, tps);
  }
  return nullptr;
}

uhdm::Typespec* CompileHelper::compileTypeParameter(
    DesignComponent* component, CompileDesign* compileDesign, Parameter* sit) {
  uhdm::Serializer& s = compileDesign->getSerializer();
  uhdm::Typespec* spec = nullptr;
  bool type_param = false;
  if (uhdm::Any* uparam = sit->getUhdmParam()) {
    if (uparam->getUhdmType() == uhdm::UhdmType::TypeParameter) {
      if (uhdm::RefTypespec* rt = ((uhdm::TypeParameter*)uparam)->getTypespec())
        spec = rt->getActual();
      type_param = true;
    } else {
      if (uhdm::RefTypespec* rt = ((uhdm::Parameter*)uparam)->getTypespec())
        spec = rt->getActual();
    }
  }

  const std::string_view pname = sit->getName();
  Parameter* param = component->getParameter(pname);

  uhdm::Any* uparam = param->getUhdmParam();
  uhdm::Typespec* override_spec = nullptr;
  if (uparam == nullptr) {
    if (type_param) {
      uhdm::TypeParameter* tp = s.make<uhdm::TypeParameter>();
      tp->setName(pname);
      param->setUhdmParam(tp);
    } else {
      uhdm::Parameter* tp = s.make<uhdm::Parameter>();
      tp->setName(pname);
      param->setUhdmParam(tp);
    }
    uparam = param->getUhdmParam();
  }

  if (type_param) {
    if (uhdm::RefTypespec* rt = ((uhdm::TypeParameter*)uparam)->getTypespec()) {
      override_spec = rt->getActual();
    }
  } else {
    if (uhdm::RefTypespec* rt = ((uhdm::Parameter*)uparam)->getTypespec()) {
      override_spec = rt->getActual();
    }
  }

  if (override_spec == nullptr) {
    override_spec = compileTypespec(component, param->getFileContent(),
                                    param->getNodeType(), InvalidNodeId,
                                    compileDesign, nullptr, nullptr, false);
  }

  if (override_spec) {
    if (type_param) {
      uhdm::TypeParameter* tparam = (uhdm::TypeParameter*)uparam;
      if (tparam->getTypespec() == nullptr) {
        uhdm::RefTypespec* override_specRef = s.make<uhdm::RefTypespec>();
        override_specRef->setParent(tparam);
        tparam->setTypespec(override_specRef);
      }
      tparam->getTypespec()->setActual(override_spec);
    } else {
      uhdm::Parameter* tparam = (uhdm::Parameter*)uparam;
      if (tparam->getTypespec() == nullptr) {
        uhdm::RefTypespec* override_specRef = s.make<uhdm::RefTypespec>();
        override_specRef->setParent(tparam);
        tparam->setTypespec(override_specRef);
      }
      tparam->getTypespec()->setActual(override_spec);
    }
    spec = override_spec;
    spec->setParent(uparam);
  }
  return spec;
}

const uhdm::Typespec* bindTypespec(Design* design, std::string_view name,
                                   SURELOG::ValuedComponentI* instance,
                                   uhdm::Serializer& s) {
  const uhdm::Typespec* result = nullptr;
  ModuleInstance* modInst = valuedcomponenti_cast<ModuleInstance*>(instance);
  while (modInst) {
    for (Parameter* param : modInst->getTypeParams()) {
      const std::string_view pname = param->getName();
      if (pname == name) {
        if (uhdm::Any* uparam = param->getUhdmParam()) {
          if (uhdm::TypeParameter* tparam =
                  any_cast<uhdm::TypeParameter>(uparam)) {
            if (const uhdm::RefTypespec* rt = tparam->getTypespec()) {
              result = rt->getActual();
            }
            uhdm::ElaboratorContext elaboratorContext(&s, false, true);
            result = any_cast<uhdm::Typespec>(
                uhdm::clone_tree((uhdm::Any*)result, &elaboratorContext));
          }
        }
        break;
      }
    }
    if (result == nullptr) {
      if (ModuleDefinition* mod = (ModuleDefinition*)modInst->getDefinition()) {
        if (Parameter* param = mod->getParameter(name)) {
          if (uhdm::Any* uparam = param->getUhdmParam()) {
            if (uhdm::TypeParameter* tparam =
                    any_cast<uhdm::TypeParameter>(uparam)) {
              if (const uhdm::RefTypespec* rt = tparam->getTypespec()) {
                result = rt->getActual();
              }
              uhdm::ElaboratorContext elaboratorContext(&s, false, true);
              result = any_cast<uhdm::Typespec>(
                  uhdm::clone_tree((uhdm::Any*)result, &elaboratorContext));
            }
          }
        }
        if (const DataType* dt = mod->getDataType(design, name)) {
          dt = dt->getActual();
          result = dt->getTypespec();
          uhdm::ElaboratorContext elaboratorContext(&s, false, true);
          result = any_cast<uhdm::Typespec>(
              uhdm::clone_tree((uhdm::Any*)result, &elaboratorContext));
        }
      }
    }
    modInst = modInst->getParent();
  }
  return result;
}

Typespec* CompileHelper::compileDatastructureTypespec(
    DesignComponent* component, const FileContent* fC, NodeId type,
    CompileDesign* compileDesign, ValuedComponentI* instance,
    std::string_view suffixname, std::string_view typeName) {
  uhdm::Serializer& s = compileDesign->getSerializer();
  uhdm::UnsupportedTypespec* tps = s.make<uhdm::UnsupportedTypespec>();
  tps->setName(typeName);
  tps->setParent(s.topScope());
  fC->populateCoreMembers(type, type, tps);
  return tps;
}

uhdm::TypespecMember* CompileHelper::buildTypespecMember(
    CompileDesign* compileDesign, const FileContent* fC, NodeId id) {
  /*
  std::string hash = fileName + ":" + name + ":" + value + ":" +
  std::to_string(line) + ":" + std::to_string(column) + ":" +
  std::to_string(eline) + ":" + std::to_string(ecolumn);
  std::unordered_map<std::string, uhdm::TypespecMember*>::iterator itr =
      m_cache_typespec_member.find(hash);
  */
  uhdm::Serializer& s = compileDesign->getSerializer();
  uhdm::TypespecMember* var = s.make<uhdm::TypespecMember>();
  var->setName(fC->SymName(id));
  if (NodeId siblingId = fC->Sibling(id)) {
    fC->populateCoreMembers(id, siblingId, var);
  } else {
    fC->populateCoreMembers(id, id, var);
  }
  return var;
}

IntTypespec* CompileHelper::buildIntTypespec(CompileDesign* compileDesign,
                                             PathId fileId,
                                             std::string_view name,
                                             std::string_view value,
                                             uint32_t line, uint16_t column,
                                             uint32_t eline, uint16_t ecolumn) {
  FileSystem* const fileSystem = m_session->getFileSystem();
  /*
  std::string hash = fileName + ":" + name + ":" + value + ":" +
  std::to_string(line)  + ":" + std::to_string(column) + ":" +
  std::to_string(eline) + ":" + std::to_string(ecolumn);
  std::unordered_map<std::string, uhdm::IntTypespec*>::iterator itr =
      m_cache_int_typespec.find(hash);
  */
  // if (itr == m_cache_int_typespec.end()) {
  uhdm::Serializer& s = compileDesign->getSerializer();
  uhdm::IntTypespec* var = s.make<uhdm::IntTypespec>();
  var->setValue(value);
  var->setFile(fileSystem->toPath(fileId));
  var->setStartLine(line);
  var->setStartColumn(column);
  var->setEndLine(eline);
  var->setEndColumn(ecolumn);
  return var;
}

uhdm::Typespec* CompileHelper::compileBuiltinTypespec(
    DesignComponent* component, const FileContent* fC, NodeId type,
    VObjectType the_type, CompileDesign* compileDesign, uhdm::Any* pstmt) {
  uhdm::Serializer& s = compileDesign->getSerializer();
  uhdm::Typespec* result = nullptr;
  NodeId sign = fC->Sibling(type);
  // 6.8 Variable declarations
  // The byte, shortint, int, integer, and longint types are signed types by
  // default.
  bool isSigned = true;
  if (sign && (fC->Type(sign) == VObjectType::paSigning_Unsigned)) {
    isSigned = false;
  }
  switch (the_type) {
    case VObjectType::paIntVec_TypeLogic:
    case VObjectType::paIntVec_TypeReg: {
      // 6.8 Variable declarations
      // Other net and variable types can be explicitly declared as signed.
      isSigned = false;
      if (sign && (fC->Type(sign) == VObjectType::paSigning_Signed)) {
        isSigned = true;
      }
      uhdm::LogicTypespec* var = s.make<uhdm::LogicTypespec>();
      var->setSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? sign : type, var);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Int: {
      uhdm::IntTypespec* var = s.make<uhdm::IntTypespec>();
      var->setSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? type : sign, var);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Integer: {
      uhdm::IntegerTypespec* var = s.make<uhdm::IntegerTypespec>();
      var->setSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? type : sign, var);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Byte: {
      uhdm::ByteTypespec* var = s.make<uhdm::ByteTypespec>();
      var->setSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? type : sign, var);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_LongInt: {
      uhdm::LongIntTypespec* var = s.make<uhdm::LongIntTypespec>();
      var->setSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? type : sign, var);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Shortint: {
      uhdm::ShortIntTypespec* var = s.make<uhdm::ShortIntTypespec>();
      var->setSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? type : sign, var);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Time: {
      uhdm::TimeTypespec* var = s.make<uhdm::TimeTypespec>();
      fC->populateCoreMembers(type, type, var);
      result = var;
      break;
    }
    case VObjectType::paIntVec_TypeBit: {
      uhdm::BitTypespec* var = s.make<uhdm::BitTypespec>();
      isSigned = false;
      if (sign && (fC->Type(sign) == VObjectType::paSigning_Signed)) {
        isSigned = true;
      }
      var->setSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? sign : type, var);
      result = var;
      break;
    }
    case VObjectType::paNonIntType_ShortReal: {
      uhdm::ShortRealTypespec* var = s.make<uhdm::ShortRealTypespec>();
      fC->populateCoreMembers(type, type, var);
      result = var;
      break;
    }
    case VObjectType::paNonIntType_Real: {
      uhdm::RealTypespec* var = s.make<uhdm::RealTypespec>();
      fC->populateCoreMembers(type, type, var);
      result = var;
      break;
    }
    case VObjectType::paString_type: {
      uhdm::StringTypespec* tps = s.make<uhdm::StringTypespec>();
      fC->populateCoreMembers(type, type, tps);
      result = tps;
      break;
    }
    default:
      uhdm::LogicTypespec* var = s.make<uhdm::LogicTypespec>();
      fC->populateCoreMembers(type, type, var);
      result = var;
      break;
  }
  result->setParent(pstmt);
  return result;
}

uhdm::Typespec* CompileHelper::getUpdatedArrayTypespec(
    const FileContent* fC, NodeId nodeId, NodeId dimensionId,
    CompileDesign* compileDesign, uhdm::RangeCollection* ranges,
    uhdm::Typespec* elemTps, uhdm::Any* pstmt, bool bPacked) {
  uhdm::Serializer& s = compileDesign->getSerializer();
  //uhdm::Typespec* retts = elemTps;
  bool dynamic = false;
  bool associative = false;
  bool queue = false;
  bool bAddRange = false;

  uhdm::Typespec* elmtp = elemTps;
  NodeId dimensionId2 = dimensionId;
  std::string_view sRefName = elemTps ? elemTps->getName() : "";
  for (auto itr = ranges->rbegin(); itr != ranges->rend(); ++itr) {
    uhdm::Range* r = *itr;
    const uhdm::Expr* rhs = r->getRightExpr();
    if (bPacked) {
      // In case of packed don't iterate. It's always static
    }
    else if (rhs->getUhdmType() == uhdm::UhdmType::Operation) {
      if (const uhdm::AnyCollection* const operands =
              static_cast<const uhdm::Operation*>(rhs)->getOperands()) {
        if (operands->size() == 1) {
          if (const uhdm::RefObj* const ro =
                  any_cast<uhdm::RefObj>(operands->front())) {
            if (ro->getActual() == nullptr) {
              // Force it to be associative if the reference object can't be
              // resolved. This will be fixed up later during binding.
              associative = true;
              bAddRange = true;
            }
          }
        }
      }
    } else if (rhs->getUhdmType() == uhdm::UhdmType::Constant) {
      const std::string_view value = rhs->getValue();
      if (value == "STRING:$") {
        queue = true;
      } else if (value == "STRING:associative") {
        associative = true;
      } else if (value == "STRING:unsized") {
        dynamic = true;
      }
    }

    uhdm::ArrayTypespec* taps = s.make<uhdm::ArrayTypespec>();
    taps->setPacked(bPacked);
    taps->setParent(pstmt);

    //fC->populateCoreMembers(nodeId, bPacked ? nodeId : dimensionId2, taps);
    taps->setFile(r->getFile());
    taps->setStartLine(r->getStartLine());
    taps->setStartColumn(r->getStartColumn());
    taps->setEndLine(r->getEndLine());
    taps->setEndColumn(r->getEndColumn());

    uhdm::RefTypespec* ert = s.make<uhdm::RefTypespec>();
    ert->setParent(taps);
    if (elmtp != nullptr) {
      ert->setActual(elmtp);
      setRefTypespecName(ert, elmtp, sRefName);
    }
    taps->setElemTypespec(ert);

    ert->setFile(r->getFile());
    ert->setStartLine(r->getStartLine());
    ert->setStartColumn(r->getStartColumn());
    ert->setEndLine(r->getEndLine());
    ert->setEndColumn(r->getEndColumn());
    //fC->populateCoreMembers(nodeId, nodeId, ert);

    elmtp = taps;
    if (associative) {
      taps->setArrayType(vpiAssocArray);
      const uhdm::Typespec* tp = nullptr;
      if (const uhdm::RefTypespec* rt = rhs->getTypespec()) {
        tp = rt->getActual();
      }

      if (tp != nullptr) {
        uhdm::RefTypespec* tpRef = s.make<uhdm::RefTypespec>();
        tpRef->setParent(taps);
        tpRef->setName(tp->getName());
        taps->setIndexTypespec(tpRef);
        tpRef->setActual(const_cast<uhdm::Typespec*>(tp));
        fC->populateCoreMembers(dimensionId, dimensionId, tpRef);
      }
      if (bAddRange) {
        taps->setRange(r);
        r->setParent(taps);
      } else {
        taps->setRange(nullptr);
      }

    } else if (queue) {
      taps->setArrayType(vpiQueueArray);
      taps->setRange(nullptr);
    } else if (dynamic) {
      taps->setArrayType(vpiDynamicArray);
      taps->setRange(nullptr);
    } else {
      taps->setArrayType(vpiStaticArray);
      taps->setRange(r);
      r->setParent(taps);
    }

    dimensionId2 = fC->Sibling(dimensionId2);
  }
  return elmtp;
}

uhdm::Typespec* CompileHelper::compileUpdatedTypespec(
    DesignComponent* component, const FileContent* fC, NodeId nodeId,
    NodeId packedId, NodeId unpackedId, CompileDesign* compileDesign,
    uhdm::Any* pstmt, uhdm::Typespec* ts) {
  if (!packedId && !unpackedId) return ts;

  uhdm::Typespec* retts = ts;
  Design* const design = compileDesign->getCompiler()->getDesign();
  if (pstmt == nullptr) pstmt = component->getUhdmModel();
  if (pstmt == nullptr) pstmt = design->getUhdmDesign();

  std::vector<uhdm::Range*>* packedDimensions = nullptr;
  if (packedId) {
    int32_t packedSize = 0;
    packedDimensions = compileRanges(component, fC, packedId, compileDesign,
                                     nullptr, nullptr, packedSize, false);
  }

  std::vector<uhdm::Range*>* unpackedDimensions = nullptr;
  if (unpackedId) {
    int32_t unpackedSize = 0;
    unpackedDimensions = compileRanges(component, fC, unpackedId, compileDesign,
                                       nullptr, nullptr, unpackedSize, false);
  }

  // LogicTypespec is exception and can have ranges as well. If yes then attach
  // those and don't create arraytypespec or packedarraytypespec.
  if (ts && (ts->getUhdmType() == uhdm::UhdmType::LogicTypespec)) {
    uhdm::LogicTypespec* lts = (uhdm::LogicTypespec*)ts;
    if ((packedDimensions != nullptr) && !packedDimensions->empty()) {
      lts->setRanges(packedDimensions);
      for (uhdm::Range* r : *packedDimensions) r->setParent(lts, true);
    }
    ts = lts;
    packedId = InvalidNodeId;
  }

  if (packedId && unpackedId) {
    uhdm::Typespec* tmpTps = getUpdatedArrayTypespec(
        fC, nodeId, packedId, compileDesign, packedDimensions, ts, pstmt);
    retts = getUpdatedArrayTypespec(fC, nodeId, unpackedId, compileDesign,
                                    unpackedDimensions, tmpTps, pstmt, false);

  } else if (unpackedId) {
    retts = getUpdatedArrayTypespec(fC, nodeId, unpackedId, compileDesign,
                                    unpackedDimensions, ts, pstmt, false);

  } else if (packedId) {
    retts = getUpdatedArrayTypespec(fC, nodeId, packedId, compileDesign,
                                    packedDimensions, ts, pstmt);
  }
  return retts;
}

uhdm::Typespec* CompileHelper::compileTypespec(
    DesignComponent* component, const FileContent* fC, NodeId id,
    NodeId unpackedDimId, CompileDesign* compileDesign, uhdm::Any* pstmt,
    ValuedComponentI* instance, bool isVariable) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  ErrorContainer* const errors = m_session->getErrorContainer();
  Design* const design = compileDesign->getCompiler()->getDesign();

  uhdm::Serializer& s = compileDesign->getSerializer();
  if (pstmt == nullptr) pstmt = component->getUhdmModel();
  if (pstmt == nullptr) pstmt = design->getUhdmDesign();

  NodeId iDataType = InvalidNodeId;
  if (fC->Type(id) != VObjectType::paData_type) {
    iDataType = fC->sl_get(id, VObjectType::paData_type);
  }
  if (!iDataType &&
      ((fC->Type(fC->Sibling(id)) == VObjectType::paData_type_or_implicit) ||
       (fC->Type(fC->Sibling(id)) == VObjectType::paData_type))) {
    iDataType = fC->Sibling(id);
  }
  if (!iDataType) {
    iDataType = id;
  }

  NodeId Packed_dimension =
      fC->sl_get(iDataType, VObjectType::paPacked_dimension);

  NodeId nodeId = id;
  if (fC->Type(id) == VObjectType::paData_type) nodeId = fC->Child(id);
  uhdm::Typespec* result = nullptr;
  NodeId type = nodeId;
  if (fC->Type(type) == VObjectType::paData_type_or_implicit) {
    type = fC->Child(type);
  }
  if (fC->Type(type) == VObjectType::paData_type) {
    if (fC->Child(type)) {
      type = fC->Child(type);
    } else {
      // Implicit type
    }
  }
  if (fC->Type(type) == VObjectType::VIRTUAL) type = fC->Sibling(type);
  VObjectType the_type = fC->Type(type);
  if (the_type == VObjectType::paData_type_or_implicit) {
    type = fC->Child(type);
    the_type = fC->Type(type);
  }
  if (the_type == VObjectType::paData_type) {
    if (fC->Child(type)) {
      type = fC->Child(type);
      if (fC->Type(type) == VObjectType::VIRTUAL) type = fC->Sibling(type);
    } else {
      // Implicit type
    }
    the_type = fC->Type(type);
  }

  bool isPacked = false;
  if (unpackedDimId &&
      (fC->Type(unpackedDimId) != VObjectType::paUnpacked_dimension) &&
      (fC->Type(unpackedDimId) != VObjectType::paVariable_dimension) &&
      (fC->Type(unpackedDimId) != VObjectType::paUnsized_dimension) &&
      (fC->Type(unpackedDimId) != VObjectType::paAssociative_dimension) &&
      (fC->Type(unpackedDimId) != VObjectType::paQueue_dimension)) {
    unpackedDimId =
        fC->sl_get(unpackedDimId, VObjectType::paUnpacked_dimension);
  }

  NodeId Packed_dimensionStartId, Packed_dimensionEndId;
  if (fC->Type(Packed_dimension) == VObjectType::paPacked_dimension) {
    isPacked = true;
    Packed_dimensionStartId = Packed_dimensionEndId = Packed_dimension;
    while (fC->Sibling(Packed_dimensionEndId))
      Packed_dimensionEndId = fC->Sibling(Packed_dimensionEndId);
  }

  int32_t size;
  switch (the_type) {
    case VObjectType::paConstant_mintypmax_expression:
    case VObjectType::paConstant_primary: {
      result = compileTypespec(component, fC, fC->Child(type), InvalidNodeId,
                               compileDesign, pstmt, instance, false);
      break;
    }
    case VObjectType::paSystem_task: {
      if (uhdm::Any* res = compileExpression(component, fC, type, compileDesign,
                                             pstmt, instance)) {
        uhdm::IntegerTypespec* var = s.make<uhdm::IntegerTypespec>();
        fC->populateCoreMembers(type, type, var);
        result = var;
        if (uhdm::Constant* constant = any_cast<uhdm::Constant*>(res)) {
          var->setValue(constant->getValue());
        } else {
          var->setExpr((uhdm::Expr*)res);
        }
      } else {
        uhdm::UnsupportedTypespec* tps = s.make<uhdm::UnsupportedTypespec>();
        tps->setParent(pstmt);
        fC->populateCoreMembers(type, type, tps);
        result = tps;
      }
      break;
    }
    case VObjectType::paEnum_base_type:
    case VObjectType::paEnum_name_declaration: {
      uhdm::Typespec* baseType = nullptr;
      if (the_type == VObjectType::paEnum_base_type) {
        baseType =
            compileTypespec(component, fC, fC->Child(type), InvalidNodeId,
                            compileDesign, pstmt, instance, isVariable);

        type = fC->Sibling(type);
      }
      result = baseType;
      break;
    }
    case VObjectType::paInterface_identifier: {
      uhdm::InterfaceTypespec* tps = s.make<uhdm::InterfaceTypespec>();
      fC->populateCoreMembers(type, type, tps);
      std::string name(fC->SymName(type));
      while (type) {
        if (fC->Type(type) == VObjectType::STRING_CONST) {
          name.append(".").append(fC->SymName(type));
        }
        type = fC->Sibling(type);
      }
      tps->setName(name);
      std::string tsName = StrCat(fC->getLibrary()->getName(), "@", name);
      if (ModuleDefinition* const def = design->getModuleDefinition(tsName)) {
        tps->setInterface(def->getUhdmModel<uhdm::Interface>());
      } else if (Modport* const mp = design->getModport(tsName)) {
        tps->setInterface(mp->getInterface());
        tps->setModport(mp->getUhdmModel());
      }
      result = tps;
      break;
    }
    case VObjectType::paSigning_Signed: {
      break;
    }
    case VObjectType::paSigning_Unsigned: {
      //  6.8 Variable declarations, implicit type
      uhdm::LogicTypespec* tps = s.make<uhdm::LogicTypespec>();
      fC->populateCoreMembers(type, type, tps);
      result = tps;

      break;
    }
    case VObjectType::paPacked_dimension: {
      // 6.8 Variable declarations, implicit type
      uhdm::LogicTypespec* tps = s.make<uhdm::LogicTypespec>();
      fC->populateCoreMembers(type, type, tps);
      result = tps;

      break;
    }
    case VObjectType::paExpression: {
      NodeId Primary = fC->Child(type);
      NodeId Primary_literal = fC->Child(Primary);
      NodeId Name = fC->Child(Primary_literal);
      if (fC->Type(Name) == VObjectType::paClass_scope) {
        result = compileTypespec(component, fC, Name, InvalidNodeId,
                                 compileDesign, pstmt, instance, isVariable);
      }
      if (instance) {
        const std::string_view name = fC->SymName(Name);
        result = (uhdm::Typespec*)bindTypespec(design, name, instance, s);
      }
      break;
    }
    case VObjectType::paPrimary_literal: {
      NodeId literal = fC->Child(type);
      if (fC->Type(literal) == VObjectType::STRING_CONST) {
        const std::string_view typeName = fC->SymName(literal);
        result = compileDatastructureTypespec(
            component, fC, type, compileDesign, instance, "", typeName);
      } else {
        uhdm::IntegerTypespec* var = s.make<uhdm::IntegerTypespec>();
        var->setValue(StrCat("INT:", fC->SymName(literal)));
        fC->populateCoreMembers(type, type, var);
        result = var;
      }
      break;
    }
    case VObjectType::paIntVec_TypeLogic:
    case VObjectType::paNetType_Wire:
    case VObjectType::paNetType_Supply0:
    case VObjectType::paNetType_Supply1:
    case VObjectType::paNetType_Tri0:
    case VObjectType::paNetType_Tri1:
    case VObjectType::paNetType_Tri:
    case VObjectType::paNetType_TriAnd:
    case VObjectType::paNetType_TriOr:
    case VObjectType::paNetType_TriReg:
    case VObjectType::paNetType_Uwire:
    case VObjectType::paNetType_Wand:
    case VObjectType::paNetType_Wor:
    case VObjectType::paIntVec_TypeReg:
    case VObjectType::paIntegerAtomType_Int:
    case VObjectType::paIntegerAtomType_Integer:
    case VObjectType::paIntegerAtomType_Byte:
    case VObjectType::paIntegerAtomType_LongInt:
    case VObjectType::paIntegerAtomType_Shortint:
    case VObjectType::paIntegerAtomType_Time:
    case VObjectType::paIntVec_TypeBit:
    case VObjectType::paNonIntType_ShortReal:
    case VObjectType::paNonIntType_Real:
    case VObjectType::paString_type: {
      result = compileBuiltinTypespec(component, fC, type, the_type,
                                      compileDesign, pstmt);
      break;
    }
    case VObjectType::paPackage_scope:
    case VObjectType::paClass_scope: {
      std::string typeName;
      NodeId class_type = fC->Child(type);
      NodeId class_name;
      if (the_type == VObjectType::paClass_scope)
        class_name = fC->Child(class_type);
      else
        class_name = class_type;
      typeName = fC->SymName(class_name);
      std::string packageName = typeName;
      typeName += "::";
      NodeId symb_id = fC->Sibling(type);
      const std::string_view name = fC->SymName(symb_id);
      typeName += name;
      Package* pack = design->getPackage(packageName);
      if (pack) {
        const DataType* dtype = pack->getDataType(design, name);
        if (dtype == nullptr) {
          ClassDefinition* classDefn = pack->getClassDefinition(name);
          dtype = (const DataType*)classDefn;
          if (classDefn) {
            result = classDefn->getUhdmTypespecModel();
            break;
          }
        }
        while ((dtype != nullptr) && (result == nullptr)) {
          if (const TypeDef* typed = datatype_cast<TypeDef>(dtype)) {
            result = typed->getTypespec();
          }
          dtype = dtype->getDefinition();
        }
        if (!result) {
          if (uhdm::ParamAssignCollection* param_assigns =
                  pack->getParamAssigns()) {
            for (uhdm::ParamAssign* param : *param_assigns) {
              const std::string_view param_name = param->getLhs()->getName();
              if (param_name == name) {
                const uhdm::Any* rhs = param->getRhs();
                if (const uhdm::Expr* exp = any_cast<const uhdm::Expr*>(rhs)) {
                  uhdm::IntTypespec* its = s.make<uhdm::IntTypespec>();
                  its->setValue(exp->getValue());
                  result = its;
                } else {
                  result = (uhdm::Typespec*)rhs;
                }
                break;
              }
            }
          }
        }
      }
      if (result == nullptr) {
        uhdm::UnsupportedTypespec* ref = s.make<uhdm::UnsupportedTypespec>();
        ref->setParent(pstmt);
        ref->setPacked(isPacked);
        ref->setName(typeName);
        fC->populateCoreMembers(class_name, symb_id, ref);
        result = ref;
      }
      break;
    }
    case VObjectType::paStruct_union: {
      NodeId struct_or_union = fC->Child(type);
      VObjectType struct_or_union_type = fC->Type(struct_or_union);
      uhdm::TypespecMemberCollection* members =
          s.makeCollection<uhdm::TypespecMember>();

      NodeId struct_or_union_member = fC->Sibling(type);
      if (fC->Type(struct_or_union_member) == VObjectType::paPacked_keyword) {
        struct_or_union_member = fC->Sibling(struct_or_union_member);
        isPacked = true;
      }

      uhdm::Typespec* structOtUnionTypespec = nullptr;
      if (struct_or_union_type == VObjectType::paStruct_keyword) {
        uhdm::StructTypespec* ts = s.make<uhdm::StructTypespec>();
        ts->setPacked(isPacked);
        ts->setMembers(members);
        result = structOtUnionTypespec = ts;
      } else {
        uhdm::UnionTypespec* ts = s.make<uhdm::UnionTypespec>();
        ts->setPacked(isPacked);
        ts->setMembers(members);
        result = structOtUnionTypespec = ts;
      }
      result->setParent(pstmt);
      fC->populateCoreMembers(id, id, result);

      if (Packed_dimensionStartId) {
        structOtUnionTypespec->setEndLine(fC->Line(Packed_dimensionStartId));
        structOtUnionTypespec->setEndColumn(
            fC->Column(Packed_dimensionStartId) - 1);
        fC->populateCoreMembers(Packed_dimensionStartId, Packed_dimensionEndId,
                                result);
      }

      while (struct_or_union_member) {
        NodeId Data_type_or_void = fC->Child(struct_or_union_member);
        NodeId Data_type = fC->Child(Data_type_or_void);
        NodeId List_of_variable_decl_assignments =
            fC->Sibling(Data_type_or_void);
        NodeId Variable_decl_assignment =
            fC->Child(List_of_variable_decl_assignments);
        while (Variable_decl_assignment) {
          uhdm::Typespec* member_ts = nullptr;

          NodeId member_name = fC->Child(Variable_decl_assignment);
          NodeId Expression = fC->Sibling(member_name);

          // Create packed and array typespec
          NodeId tmpUnpackedId = InvalidNodeId;
          if (Expression &&
              (fC->Type(Expression) == VObjectType::paVariable_dimension)) {
            NodeId Unpacked_dimension = fC->Child(Expression);
            if (fC->Type(Unpacked_dimension) ==
                VObjectType::paUnpacked_dimension) {
              tmpUnpackedId = Unpacked_dimension;

              if (isPacked) {
                Location loc1(
                    fC->getFileId(), fC->Line(Unpacked_dimension),
                    fC->Column(Unpacked_dimension),
                    symbols->registerSymbol(fC->SymName(member_name)));
                Error err(ErrorDefinition::COMP_UNPACKED_IN_PACKED, loc1);
                errors->addError(err);
              }
            }
          }

          if (Data_type) {
            member_ts = compileTypespec(component, fC, Data_type, tmpUnpackedId,
                                        compileDesign, result, instance, false);
          } else {
            uhdm::VoidTypespec* tps = s.make<uhdm::VoidTypespec>();
            tps->setParent(result);
            fC->populateCoreMembers(Data_type_or_void, Data_type_or_void, tps);
            member_ts = tps;
          }

          uhdm::TypespecMember* m =
              buildTypespecMember(compileDesign, fC, member_name);
          m->setParent(structOtUnionTypespec);
          if (member_ts != nullptr) {
            if (m->getTypespec() == nullptr) {
              uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
              tsRef->setParent(m);
              setRefTypespecName(tsRef, member_ts, fC->SymName(Data_type));
              fC->populateCoreMembers(Data_type_or_void, Data_type_or_void,
                                      tsRef);
              m->setTypespec(tsRef);
            }
            m->getTypespec()->setActual(member_ts);
          }
          if (Expression &&
              (fC->Type(Expression) != VObjectType::paVariable_dimension)) {
            if (uhdm::Any* ex =
                    compileExpression(component, fC, Expression, compileDesign,
                                      m, instance, false)) {
              m->setDefaultValue((uhdm::Expr*)ex);
            }
          }

          members->emplace_back(m);
          Variable_decl_assignment = fC->Sibling(Variable_decl_assignment);
        }
        struct_or_union_member = fC->Sibling(struct_or_union_member);
      }
      break;
    }
    case VObjectType::paSimple_type:
    case VObjectType::paPs_type_identifier:
    case VObjectType::paInteger_type: {
      result = compileTypespec(component, fC, fC->Child(type), InvalidNodeId,
                               compileDesign, pstmt, instance, false);
      break;
    }
    case VObjectType::STRING_CONST: {
      const std::string_view typeName = fC->SymName(type);
      if (typeName == "logic") {
        uhdm::LogicTypespec* var = s.make<uhdm::LogicTypespec>();
        fC->populateCoreMembers(type, type, var);
        result = var;
      } else if (typeName == "bit") {
        uhdm::BitTypespec* var = s.make<uhdm::BitTypespec>();
        fC->populateCoreMembers(type, type, var);
        result = var;
      } else if (typeName == "byte") {
        uhdm::ByteTypespec* var = s.make<uhdm::ByteTypespec>();
        fC->populateCoreMembers(type, type, var);
        result = var;
      }
      if (!result && component) {
        if (uhdm::ParamAssignCollection* param_assigns =
                component->getParamAssigns()) {
          for (uhdm::ParamAssign* param : *param_assigns) {
            const std::string_view param_name = param->getLhs()->getName();
            if (param_name == typeName) {
              const uhdm::Any* rhs = param->getRhs();
              if (const uhdm::Constant* exp =
                      any_cast<const uhdm::Constant*>(rhs)) {
                uhdm::IntTypespec* its = buildIntTypespec(
                    compileDesign,
                    fileSystem->toPathId(param->getFile(), symbols), typeName,
                    exp->getValue(), param->getStartLine(),
                    param->getStartColumn(), param->getStartLine(),
                    param->getStartColumn());
                result = its;
              } else if (const uhdm::Operation* exp =
                             any_cast<const uhdm::Operation*>(rhs)) {
                if (const uhdm::RefTypespec* rt = exp->getTypespec())
                  result = const_cast<uhdm::Typespec*>(rt->getActual());
              }
              break;
            }
          }
        }
      }
      if (!result) {
        if (component) {
          ClassDefinition* cl = design->getClassDefinition(typeName);
          if (cl == nullptr) {
            cl = design->getClassDefinition(
                StrCat(component->getName(), "::", typeName));
          }
          if (cl == nullptr) {
            if (const DesignComponent* p =
                    valuedcomponenti_cast<const DesignComponent*>(
                        component->getParentScope())) {
              cl = design->getClassDefinition(
                  StrCat(p->getName(), "::", typeName));
            }
          }
          if (cl) {
            result = cl->getUhdmTypespecModel();
          }
        }
      }
      if (result == nullptr) {
        result = compileDatastructureTypespec(
            component, fC, type, compileDesign, instance, "", typeName);
        if (result) {
          fC->populateCoreMembers(type, type, result);
        }
      }
      if ((!result) && component) {
        if (uhdm::AnyCollection* params = component->getParameters()) {
          for (uhdm::Any* param : *params) {
            if ((param->getUhdmType() == uhdm::UhdmType::TypeParameter) &&
                (param->getName() == typeName)) {
              result = (uhdm::Typespec*)param;
              break;
            }
          }
        }
      }

      break;
    }
    case VObjectType::paConstant_expression: {
      if (uhdm::Expr* exp = (uhdm::Expr*)compileExpression(
              component, fC, type, compileDesign, pstmt, instance, true)) {
        if (exp->getUhdmType() == uhdm::UhdmType::RefObj) {
          result =
              compileTypespec(component, fC, fC->Child(type), InvalidNodeId,
                              compileDesign, result, instance, false);
        } else {
          uhdm::IntegerTypespec* var = s.make<uhdm::IntegerTypespec>();
          if (exp->getUhdmType() == uhdm::UhdmType::Constant) {
            var->setValue(exp->getValue());
          } else {
            var->setExpr(exp);
            exp->setParent(var, true);
          }
          fC->populateCoreMembers(type, type, var);
          result = var;
        }
      }
      break;
    }
    case VObjectType::paChandle_type: {
      uhdm::ChandleTypespec* tps = s.make<uhdm::ChandleTypespec>();
      fC->populateCoreMembers(type, type, tps);
      result = tps;
      break;
    }
    case VObjectType::paConstant_range: {
      uhdm::LogicTypespec* tps = s.make<uhdm::LogicTypespec>();
      fC->populateCoreMembers(type, type, tps);
      if (uhdm::RangeCollection* ranges = compileRanges(
              component, fC, type, compileDesign, tps, instance, size, false)) {
        if (!ranges->empty()) {
          tps->setRanges(ranges);
          for (uhdm::Range* r : *ranges) r->setParent(tps, true);
        }
      }
      result = tps;
      break;
    }
    case VObjectType::paEvent_type: {
      uhdm::EventTypespec* tps = s.make<uhdm::EventTypespec>();
      fC->populateCoreMembers(type, type, tps);
      result = tps;
      break;
    }
    case VObjectType::paNonIntType_RealTime: {
      uhdm::TimeTypespec* tps = s.make<uhdm::TimeTypespec>();
      fC->populateCoreMembers(type, type, tps);
      result = tps;
      break;
    }
    case VObjectType::paType_reference: {
      NodeId child = fC->Child(type);
      if (fC->Type(child) == VObjectType::paExpression) {
        uhdm::Expr* exp = (uhdm::Expr*)compileExpression(
            component, fC, child, compileDesign, nullptr, instance, false);
        if (exp) {
          uhdm::UhdmType typ = exp->getUhdmType();
          if (typ == uhdm::UhdmType::RefObj) {
            result = compileTypespec(component, fC, child, InvalidNodeId,
                                     compileDesign, result, instance, false);
          } else if (typ == uhdm::UhdmType::Constant) {
            uhdm::Constant* c = (uhdm::Constant*)exp;
            int32_t ctype = c->getConstType();
            if (ctype == vpiIntConst || ctype == vpiDecConst) {
              uhdm::IntTypespec* tps = s.make<uhdm::IntTypespec>();
              tps->setSigned(true);
              result = tps;
            } else if (ctype == vpiUIntConst || ctype == vpiBinaryConst ||
                       ctype == vpiHexConst || ctype == vpiOctConst) {
              uhdm::IntTypespec* tps = s.make<uhdm::IntTypespec>();
              result = tps;
            } else if (ctype == vpiRealConst) {
              uhdm::RealTypespec* tps = s.make<uhdm::RealTypespec>();
              result = tps;
            } else if (ctype == vpiStringConst) {
              uhdm::StringTypespec* tps = s.make<uhdm::StringTypespec>();
              result = tps;
            } else if (ctype == vpiTimeConst) {
              uhdm::TimeTypespec* tps = s.make<uhdm::TimeTypespec>();
              result = tps;
            }
            fC->populateCoreMembers(type, type, result);
          }
        } else {
          std::string lineText;
          fileSystem->readLine(fC->getFileId(), fC->Line(type), lineText);
          Location loc(fC->getFileId(type), fC->Line(type), fC->Column(type),
                       symbols->registerSymbol(
                           StrCat("<", fC->printObject(type), "> ", lineText)));
          Error err(ErrorDefinition::UHDM_UNSUPPORTED_TYPE, loc);
          errors->addError(err);
        }
      } else {
        result = compileTypespec(component, fC, child, InvalidNodeId,
                                 compileDesign, result, instance, false);
      }
      break;
    }
    case VObjectType::paData_type_or_implicit: {
      uhdm::LogicTypespec* tps = s.make<uhdm::LogicTypespec>();
      fC->populateCoreMembers(type, type, tps);
      result = tps;
      break;
    }
    case VObjectType::paImplicit_data_type: {
      // Interconnect
      uhdm::LogicTypespec* tps = s.make<uhdm::LogicTypespec>();
      fC->populateCoreMembers(type, type, tps);
      result = tps;
      break;
    }
    default:
      if (type) {
        std::string lineText;
        fileSystem->readLine(fC->getFileId(), fC->Line(type), lineText);
        Location loc(fC->getFileId(type), fC->Line(type), fC->Column(type),
                     symbols->registerSymbol(
                         StrCat("<", fC->printObject(type), "> ", lineText)));
        Error err(ErrorDefinition::UHDM_UNSUPPORTED_TYPE, loc);
        errors->addError(err);
      }
      break;
  };

  if (result != nullptr) {
    result->setParent(pstmt);
  }
  result = compileUpdatedTypespec(component, fC, id, Packed_dimension,
                                  unpackedDimId, compileDesign, pstmt, result);
  if (result != nullptr) {
    result->setParent(pstmt);
  }

  return result;
}

uhdm::Typespec* CompileHelper::elabTypespec(DesignComponent* component,
                                            uhdm::Typespec* spec,
                                            CompileDesign* compileDesign,
                                            uhdm::Any* pexpr,
                                            ValuedComponentI* instance) {
  uhdm::Serializer& s = compileDesign->getSerializer();
  uhdm::Typespec* result = spec;
  uhdm::UhdmType type = spec->getUhdmType();
  uhdm::RangeCollection* ranges = nullptr;
  switch (type) {
    case uhdm::UhdmType::BitTypespec: {
      uhdm::BitTypespec* tps = (uhdm::BitTypespec*)spec;
      ranges = tps->getRanges();
      if (ranges) {
        uhdm::ElaboratorContext elaboratorContext(&s, false, true);
        uhdm::BitTypespec* res = any_cast<uhdm::BitTypespec>(
            uhdm::clone_tree((uhdm::Any*)spec, &elaboratorContext));
        ranges = res->getRanges();
        result = res;
      }
      break;
    }
    case uhdm::UhdmType::LogicTypespec: {
      uhdm::LogicTypespec* tps = (uhdm::LogicTypespec*)spec;
      ranges = tps->getRanges();
      if (ranges) {
        uhdm::ElaboratorContext elaboratorContext(&s, false, true);
        uhdm::LogicTypespec* res = any_cast<uhdm::LogicTypespec>(
            uhdm::clone_tree((uhdm::Any*)spec, &elaboratorContext));
        ranges = res->getRanges();
        result = res;
      }
      break;
    }
    case uhdm::UhdmType::ArrayTypespec: {
      uhdm::ArrayTypespec* tps = (uhdm::ArrayTypespec*)spec;
      ranges = tps->getRanges();
      if (ranges) {
        uhdm::ElaboratorContext elaboratorContext(&s, false, true);
        uhdm::ArrayTypespec* res = any_cast<uhdm::ArrayTypespec>(
            uhdm::clone_tree((uhdm::Any*)spec, &elaboratorContext));
        ranges = res->getRanges();
        result = res;
      }
      break;
    }
    default:
      break;
  }
  return result;
}

bool CompileHelper::isOverloaded(const uhdm::Any* expr,
                                 CompileDesign* compileDesign,
                                 ValuedComponentI* instance) {
  if (instance == nullptr) return false;
  ModuleInstance* inst = valuedcomponenti_cast<ModuleInstance*>(instance);
  if (inst == nullptr) return false;
  std::stack<const uhdm::Any*> stack;
  const uhdm::Any* tmp = expr;
  stack.push(tmp);
  while (!stack.empty()) {
    tmp = stack.top();
    stack.pop();
    uhdm::UhdmType type = tmp->getUhdmType();
    switch (type) {
      case uhdm::UhdmType::Range: {
        uhdm::Range* r = (uhdm::Range*)tmp;
        stack.push(r->getLeftExpr());
        stack.push(r->getRightExpr());
        break;
      }
      case uhdm::UhdmType::Constant: {
        if (const uhdm::RefTypespec* rt =
                ((uhdm::Constant*)tmp)->getTypespec()) {
          if (const uhdm::Any* tp = rt->getActual()) {
            stack.push(tp);
          }
        }
        break;
      }
      case uhdm::UhdmType::TypedefTypespec: {
        stack.push(tmp);
        break;
      }
      case uhdm::UhdmType::LogicTypespec: {
        uhdm::LogicTypespec* tps = (uhdm::LogicTypespec*)tmp;
        if (tps->getRanges()) {
          for (auto op : *tps->getRanges()) {
            stack.push(op);
          }
        }
        break;
      }
      case uhdm::UhdmType::BitTypespec: {
        uhdm::BitTypespec* tps = (uhdm::BitTypespec*)tmp;
        if (tps->getRanges()) {
          for (auto op : *tps->getRanges()) {
            stack.push(op);
          }
        }
        break;
      }
      case uhdm::UhdmType::ArrayTypespec: {
        uhdm::ArrayTypespec* tps = (uhdm::ArrayTypespec*)tmp;
        if (tps->getRanges()) {
          for (auto op : *tps->getRanges()) {
            stack.push(op);
          }
        }
        if (const uhdm::RefTypespec* rt = tps->getElemTypespec()) {
          if (const uhdm::Any* etps = rt->getActual()) {
            stack.push(etps);
          }
        }
        break;
      }
      case uhdm::UhdmType::Operation: {
        uhdm::Operation* oper = (uhdm::Operation*)tmp;
        for (auto op : *oper->getOperands()) {
          stack.push(op);
        }
        break;
      }
      default:
        break;
    }
  }
  return false;
}

}  // namespace SURELOG
