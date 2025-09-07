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
#include "Surelog/Design/Function.h"
#include "Surelog/Design/ModuleDefinition.h"
#include "Surelog/Design/ModuleInstance.h"
#include "Surelog/Design/Netlist.h"
#include "Surelog/Design/ParamAssign.h"
#include "Surelog/Design/Parameter.h"
#include "Surelog/Design/Signal.h"
#include "Surelog/Design/SimpleType.h"
#include "Surelog/Design/Struct.h"
#include "Surelog/Design/Task.h"
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
#include <uhdm/ExprEval.h>
#include <uhdm/clone_tree.h>
#include <uhdm/expr.h>
#include <uhdm/uhdm.h>
#include <uhdm/uhdm_types.h>
#include <uhdm/vpi_user.h>

#include <cstdint>
#include <stack>
#include <string>
#include <string_view>
#include <vector>

namespace SURELOG {

using namespace uhdm;  // NOLINT (using a bunch of them)

uhdm::Any* CompileHelper::compileVariable(
    DesignComponent* component, const FileContent* fC, NodeId declarationId,
    NodeId nameId, NodeId unpackedDimId, CompileDesign* compileDesign,
    Reduce reduce, uhdm::Any* pstmt, SURELOG::ValuedComponentI* instance,
    bool muteErrors) {
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
                         compileDesign, reduce, pstmt, instance, true);
  }
  bool isSigned = true;
  const NodeId signId = fC->Sibling(variable);
  if (signId && (fC->Type(signId) == VObjectType::paSigning_Unsigned)) {
    isSigned = false;
  }
  switch (the_type) {
    case VObjectType::STRING_CONST:
    case VObjectType::paChandle_type: {
      const std::string_view typeName = fC->SymName(variable);

      if (const DataType* dt = component->getDataType(design, typeName)) {
        dt = dt->getActual();
        if (uhdm::Typespec* tps = dt->getTypespec()) {
          result = compileVariable(component, fC, compileDesign, nameId,
                                   the_type, declarationId, ts);
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
          uhdm::ClassVar* var = s.make<uhdm::ClassVar>();
          if (ts == nullptr) {
            uhdm::ClassTypespec* tps = s.make<uhdm::ClassTypespec>();
            tps->setClassDefn(cl->getUhdmModel<uhdm::ClassDefn>());
            tps->setName(typespecName);
            tps->setParent(pstmt);
            fC->populateCoreMembers(variable, variable, tps);
            ts = tps;
          }
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
        uhdm::ClassVar* var = s.make<uhdm::ClassVar>();
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
          uhdm::ChandleVar* var = s.make<uhdm::ChandleVar>();
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
          uhdm::Variables* ref = nullptr;
          if (ts && (ts->getUhdmType() == uhdm::UhdmType::ArrayTypespec)) {
            ref = s.make<uhdm::ArrayVar>();
          } else if (ts && (ts->getUhdmType() ==
                            uhdm::UhdmType::PackedArrayTypespec)) {
            ref = s.make<uhdm::PackedArrayVar>();
          } else {
            ref = s.make<uhdm::RefVar>();
          }
          ref->setName(fC->SymName(nameId));
          fC->populateCoreMembers(nameId, nameId, ref);
          if (ts != nullptr) {
            uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
            fC->populateCoreMembers(declarationId, declarationId, tsRef);
            tsRef->setParent(ref);
            tsRef->setActual(ts);
            tsRef->setName(typeName);
            fC->populateCoreMembers(declarationId, declarationId, tsRef);
            ref->setTypespec(tsRef);
          }
          result = ref;
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
            var = (uhdm::Variables*)compileVariable(
                component, fC, compileDesign, nameId, the_type, declarationId,
                tps);
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
                                    the_type, declarationId, tps);
              break;
            }
            dtype = dtype->getDefinition();
          }
        }
      }

      if (var == nullptr) {
        const std::string completeName = StrCat(packageName, "::", typeName);

        Variables* const v = s.make<uhdm::ClassVar>();
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
                               declarationId, ts);
      if (pstmt) result->setParent(pstmt, true);
      // Implicit type
      if ((result == nullptr) && declarationId) {
        uhdm::LogicVar* var = s.make<uhdm::LogicVar>();
        var->setParent(pstmt);

        if (ts == nullptr) {
          uhdm::LogicTypespec* lts = s.make<uhdm::LogicTypespec>();
          lts->setSigned(isSigned);
          lts->setParent(var);
          ts = lts;
        }

        uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
        tsRef->setParent(var);
        tsRef->setActual(ts);
        fC->populateCoreMembers(declarationId, declarationId, tsRef);
        var->setTypespec(tsRef);

        result = var;
      } else if (Variables* const var = any_cast<Variables>(result)) {
        var->setSigned(isSigned);
      } else if (Nets* const nets = any_cast<Nets>(result)) {
        nets->setSigned(isSigned);
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

uhdm::Any* CompileHelper::compileVariable(DesignComponent* component,
                                          const FileContent* fC,
                                          CompileDesign* compileDesign,
                                          NodeId nameId, VObjectType subnettype,
                                          NodeId typespecId,
                                          uhdm::Typespec* tps) {
  uhdm::Serializer& s = compileDesign->getSerializer();
  Design* const design = compileDesign->getCompiler()->getDesign();
  uhdm::Any* pscope = component->getUhdmModel();
  if (pscope == nullptr) pscope = design->getUhdmDesign();

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
      case uhdm::UhdmType::ArrayTypespec: {
        if (pscope && (pscope->getUhdmType() == uhdm::UhdmType::Module)) {
          obj = s.make<uhdm::ArrayNet>();
        } else {
          obj = s.make<uhdm::ArrayVar>();
        }
      } break;
      case uhdm::UhdmType::PackedArrayTypespec: {
        if (pscope && (pscope->getUhdmType() == uhdm::UhdmType::Module)) {
          obj = s.make<uhdm::PackedArrayNet>();
        } else {
          obj = s.make<uhdm::PackedArrayVar>();
        }
      } break;
      case uhdm::UhdmType::StructTypespec: {
        if (pscope && (pscope->getUhdmType() == uhdm::UhdmType::Module)) {
          obj = s.make<uhdm::StructNet>();
        } else {
          obj = s.make<uhdm::StructVar>();
        }
      } break;
      case uhdm::UhdmType::LogicTypespec: {
        if (pscope && (pscope->getUhdmType() == uhdm::UhdmType::Module)) {
          obj = s.make<uhdm::LogicNet>();
        } else {
          obj = s.make<uhdm::LogicVar>();
        }
      } break;
      case uhdm::UhdmType::EnumTypespec: {
        if (pscope && (pscope->getUhdmType() == uhdm::UhdmType::Module)) {
          obj = s.make<uhdm::EnumNet>();
        } else {
          obj = s.make<uhdm::EnumVar>();
        }
      } break;
      case uhdm::UhdmType::BitTypespec: {
        obj = s.make<uhdm::BitVar>();
      } break;
      case uhdm::UhdmType::ByteTypespec: {
        obj = s.make<uhdm::ByteVar>();
      } break;
      case uhdm::UhdmType::RealTypespec: {
        obj = s.make<uhdm::RealVar>();
      } break;
      case uhdm::UhdmType::ShortRealTypespec: {
        obj = s.make<uhdm::ShortRealVar>();
      } break;
      case uhdm::UhdmType::IntTypespec: {
        obj = s.make<uhdm::IntVar>();
      } break;
      case uhdm::UhdmType::IntegerTypespec: {
        if (pscope && (pscope->getUhdmType() == uhdm::UhdmType::Module)) {
          obj = s.make<uhdm::IntegerNet>();
        } else {
          obj = s.make<uhdm::IntegerVar>();
        }
      } break;
      case uhdm::UhdmType::LongIntTypespec: {
        obj = s.make<uhdm::LongIntVar>();
      } break;
      case uhdm::UhdmType::ShortIntTypespec: {
        obj = s.make<uhdm::ShortIntVar>();
      } break;
      case uhdm::UhdmType::StringTypespec: {
        obj = s.make<uhdm::StringVar>();
      } break;
      case uhdm::UhdmType::TimeTypespec: {
        if (pscope && (pscope->getUhdmType() == uhdm::UhdmType::Module)) {
          obj = s.make<uhdm::TimeNet>();
        } else {
          obj = s.make<uhdm::TimeVar>();
        }
      } break;
      case uhdm::UhdmType::UnionTypespec: {
        obj = s.make<uhdm::UnionVar>();
      } break;
      case uhdm::UhdmType::ClassTypespec: {
        obj = s.make<uhdm::ClassVar>();
      } break;
      case uhdm::UhdmType::InterfaceTypespec: {
        obj = s.make<uhdm::VirtualInterfaceVar>();
      } break;
      case uhdm::UhdmType::VoidTypespec: {
        obj = s.make<uhdm::LogicVar>();
      } break;
      default:
        break;
    }
  }

  if (obj == nullptr) {
    if (subnettype == VObjectType::paIntegerAtomType_Shortint) {
      obj = s.make<uhdm::ShortIntVar>();
    } else if (subnettype == VObjectType::paIntegerAtomType_Int) {
      obj = s.make<uhdm::IntVar>();
    } else if (subnettype == VObjectType::paIntegerAtomType_Integer) {
      obj = s.make<uhdm::IntegerVar>();
    } else if (subnettype == VObjectType::paIntegerAtomType_LongInt) {
      obj = s.make<uhdm::LongIntVar>();
    } else if (subnettype == VObjectType::paIntegerAtomType_Time) {
      obj = s.make<uhdm::TimeVar>();
    } else if (subnettype == VObjectType::paIntVec_TypeBit) {
      obj = s.make<uhdm::BitVar>();
    } else if (subnettype == VObjectType::paIntegerAtomType_Byte) {
      obj = s.make<uhdm::ByteVar>();
    } else if (subnettype == VObjectType::paNonIntType_ShortReal) {
      obj = s.make<uhdm::ShortRealVar>();
    } else if (subnettype == VObjectType::paNonIntType_Real) {
      obj = s.make<uhdm::RealVar>();
    } else if (subnettype == VObjectType::paNonIntType_RealTime) {
      obj = s.make<uhdm::TimeVar>();
    } else if (subnettype == VObjectType::paString_type) {
      obj = s.make<uhdm::StringVar>();
    } else if (subnettype == VObjectType::paChandle_type) {
      obj = s.make<uhdm::ChandleVar>();
    } else if (subnettype == VObjectType::paIntVec_TypeLogic) {
      obj = s.make<uhdm::LogicVar>();
    } else if (subnettype == VObjectType::paEvent_type) {
      uhdm::NamedEvent* event = s.make<uhdm::NamedEvent>();
      event->setName(signame);
      return event;
    }
  }
  // default type (fallback)
  if (obj == nullptr) {
    if ((pscope != nullptr) &&
        (pscope->getUhdmType() == uhdm::UhdmType::Module)) {
      obj = s.make<uhdm::LogicNet>();
    } else {
      obj = s.make<uhdm::LogicVar>();
    }
  }

  if (Variables* const var = any_cast<Variables>(obj)) {
    var->setName(signame);
  } else if (Nets* const nets = any_cast<Nets>(obj)) {
    nets->setName(signame);
  }

  if (tps != nullptr) {
    if (uhdm::Expr* const e = any_cast<uhdm::Expr>(obj)) {
      tpsRef = s.make<uhdm::RefTypespec>();
      tpsRef->setParent(e);
      tpsRef->setActual(tps);
      e->setTypespec(tpsRef);
      tpsRef->setName(fC->SymName(typespecId));
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
  uhdm::Any* pscope = component->getUhdmModel();
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
                                Reduce::Yes, nullptr, nullptr, true);
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
  obj = compileVariable(component, fC, compileDesign, signalId, subnettype,
                        typespecId, tps);
  if (SimpleExpr* const se = any_cast<SimpleExpr>(obj)) {
    if (uhdm::AttributeCollection* const attributes = sig->attributes()) {
      se->setAttributes(attributes);
      for (uhdm::Attribute* attribute : *attributes) attribute->setParent(obj);
    }
  }

  if (Nets* const nets = any_cast<Nets>(obj)) {
    nets->setSigned(sig->isSigned());
    nets->setNetType(UhdmWriter::getVpiNetType(sig->getType()));
  }

  if (Variables* const var = any_cast<Variables>(obj)) {
    var->setSigned(sig->isSigned());
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
          Reduce::Yes, uhdmScope, nullptr, false)) {
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
    override_spec = compileTypespec(
        component, param->getFileContent(), param->getNodeType(), InvalidNodeId,
        compileDesign, Reduce::Yes, nullptr, nullptr, false);
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
    CompileDesign* compileDesign, Reduce reduce,
    SURELOG::ValuedComponentI* instance, std::string_view suffixname,
    std::string_view typeName) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();
  CommandLineParser* const clp = m_session->getCommandLineParser();
  Design* const design = compileDesign->getCompiler()->getDesign();
  uhdm::Any* pscope = component->getUhdmModel();
  if (pscope == nullptr) pscope = design->getUhdmDesign();
  uhdm::Serializer& s = compileDesign->getSerializer();
  uhdm::Typespec* result = nullptr;
  if (component) {
    const DataType* dt = component->getDataType(design, typeName);
    if (dt == nullptr) {
      const std::string_view libName = fC->getLibrary()->getName();
      dt = design->getClassDefinition(StrCat(libName, "@", typeName));
      if (dt == nullptr) {
        dt = design->getClassDefinition(
            StrCat(component->getName(), "::", typeName));
      }
      if (dt == nullptr) {
        if (component->getParentScope())
          dt = design->getClassDefinition(
              StrCat(((DesignComponent*)component->getParentScope())->getName(),
                     "::", typeName));
      }
      if (dt == nullptr) {
        dt = design->getClassDefinition(typeName);
      }
      if (dt == nullptr) {
        Parameter* p = component->getParameter(typeName);
        if (p && p->getUhdmParam() &&
            (p->getUhdmParam()->getUhdmType() == uhdm::UhdmType::TypeParameter))
          dt = p;
      }
      if (dt == nullptr) {
        for (ParamAssign* passign : component->getParamAssignVec()) {
          const FileContent* fCP = passign->getFileContent();
          if (fCP->SymName(passign->getParamId()) == typeName) {
            uhdm::ParamAssign* param_assign = passign->getUhdmParamAssign();
            uhdm::Parameter* lhs = (uhdm::Parameter*)param_assign->getLhs();
            if (uhdm::RefTypespec* rt = lhs->getTypespec()) {
              result = rt->getActual();
            }
            if (result == nullptr) {
              if (uhdm::IntTypespec* tps = buildIntTypespec(
                      compileDesign, fC->getFileId(), typeName, "",
                      fC->Line(type), fC->Column(type), fC->EndLine(type),
                      fC->EndColumn(type))) {
                result = tps;
              }
            }
            if (result->getUhdmType() == uhdm::UhdmType::IntTypespec) {
              uhdm::IntTypespec* ts = (uhdm::IntTypespec*)result;
              uhdm::RefObj* ref = s.make<uhdm::RefObj>();
              ref->setActual(lhs);
              ref->setName(typeName);
              ref->setParent(ts);
              ts->setExpr(ref);
              fC->populateCoreMembers(type, type, ref);
            }
            return result;
          }
        }
      }
      if (dt == nullptr) {
        for (Signal* sig : component->getPorts()) {
          // Interface port type
          if ((sig->getName() == typeName) && sig->getInterfaceTypeNameId()) {
            std::string suffixname;
            std::string_view typeName2 = typeName;
            if (fC->Type(sig->getInterfaceTypeNameId()) ==
                VObjectType::STRING_CONST) {
              typeName2 = fC->SymName(sig->getInterfaceTypeNameId());
            }
            NodeId suffixNode;
            if ((suffixNode = fC->Sibling(type))) {
              if (fC->Type(suffixNode) == VObjectType::STRING_CONST) {
                suffixname = fC->SymName(suffixNode);
              } else if (fC->Type(suffixNode) ==
                         VObjectType::paConstant_bit_select) {
                suffixNode = fC->Sibling(suffixNode);
                if (fC->Type(suffixNode) == VObjectType::STRING_CONST) {
                  suffixname = fC->SymName(suffixNode);
                }
              }
            }
            uhdm::Typespec* tmp = compileDatastructureTypespec(
                component, fC, sig->getInterfaceTypeNameId(), compileDesign,
                reduce, instance, suffixname, typeName2);
            if (tmp) {
              if (tmp->getUhdmType() == uhdm::UhdmType::InterfaceTypespec) {
                if (!suffixname.empty()) {
                  Location loc1(fC->getFileId(), fC->Line(suffixNode),
                                fC->Column(suffixNode),
                                symbols->registerSymbol(suffixname));
                  const std::string_view libName = fC->getLibrary()->getName();
                  ModuleDefinition* def = design->getModuleDefinition(
                      StrCat(libName, "@", typeName2));
                  const FileContent* interF = def->getFileContents()[0];
                  Location loc2(interF->getFileId(),
                                interF->Line(def->getNodeIds()[0]),
                                interF->Column(def->getNodeIds()[0]),
                                symbols->registerSymbol(typeName2));
                  Error err(ErrorDefinition::ELAB_UNKNOWN_INTERFACE_MEMBER,
                            loc1, loc2);
                  errors->addError(err);
                }
              }
              return tmp;
            }
          }
        }
      }
    }
    if (dt == nullptr) {
      if (!clp->fileUnit()) {
        for (const auto& fC : design->getAllFileContents()) {
          if (const DataType* dt1 = fC.second->getDataType(design, typeName)) {
            dt = dt1;
            break;
          }
        }
      }
    }

    TypeDef* parent_tpd = nullptr;
    while (dt) {
      if (const TypeDef* tpd = datatype_cast<TypeDef>(dt)) {
        parent_tpd = (TypeDef*)tpd;
        if (parent_tpd->getTypespec()) {
          result = parent_tpd->getTypespec();
          break;
        }
      } else if (const Struct* st = datatype_cast<Struct>(dt)) {
        result = st->getTypespec();
        if (!suffixname.empty()) {
          uhdm::StructTypespec* tpss = (uhdm::StructTypespec*)result;
          for (uhdm::TypespecMember* memb : *tpss->getMembers()) {
            if (memb->getName() == suffixname) {
              if (uhdm::RefTypespec* rt = memb->getTypespec()) {
                result = rt->getActual();
              }
              break;
            }
          }
        }
        break;
      } else if (const Enum* en = datatype_cast<Enum>(dt)) {
        result = en->getTypespec();
        break;
      } else if (const Union* un = datatype_cast<Union>(dt)) {
        result = un->getTypespec();
        break;
      } else if (const DummyType* un = datatype_cast<DummyType>(dt)) {
        result = un->getTypespec();
      } else if (const SimpleType* sit = datatype_cast<SimpleType>(dt)) {
        result = sit->getTypespec();
        if ((m_elaborate == Elaborate::Yes) && parent_tpd && result) {
          uhdm::ElaboratorContext elaboratorContext(&s, false, true);
          if (uhdm::Typespec* new_result = any_cast<uhdm::Typespec>(
                  uhdm::clone_tree((uhdm::Any*)result, &elaboratorContext))) {
            if (uhdm::TypedefTypespec* const tt =
                    any_cast<uhdm::TypedefTypespec>(new_result)) {
              if (tt->getTypedefAlias() == nullptr) {
                uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
                fC->populateCoreMembers(type, type, tsRef);
                tsRef->setParent(new_result);
                tt->setTypedefAlias(tsRef);
              }
              tt->getTypedefAlias()->setActual(result);
            }
            result = new_result;
          }
        }
        break;
      } else if (/*const uhdm::Parameter* par = */ datatype_cast<Parameter>(
          dt)) {
        // Prevent circular definition
        return nullptr;
      } else if (const ClassDefinition* classDefn =
                     datatype_cast<ClassDefinition>(dt)) {
        uhdm::ClassTypespec* ref = s.make<uhdm::ClassTypespec>();
        ref->setClassDefn(classDefn->getUhdmModel<uhdm::ClassDefn>());
        ref->setName(typeName);
        ref->setParent(pscope);
        fC->populateCoreMembers(type, type, ref);
        result = ref;

        const FileContent* actualFC = fC;
        NodeId param = fC->Sibling(type);
        if (parent_tpd) {
          actualFC = parent_tpd->getFileContent();
          NodeId n = parent_tpd->getDefinitionNode();
          param = actualFC->Sibling(n);
        }
        if (param && (actualFC->Type(param) !=
                      VObjectType::paNet_decl_assignment_list)) {
          uhdm::AnyCollection* params = ref->getParameters(true);
          uhdm::ParamAssignCollection* assigns = ref->getParamAssigns(true);
          uint32_t index = 0;
          NodeId Parameter_value_assignment = param;
          NodeId List_of_parameter_assignments =
              actualFC->Child(Parameter_value_assignment);
          NodeId Ordered_parameter_assignment =
              actualFC->Child(List_of_parameter_assignments);
          if (Ordered_parameter_assignment &&
              (actualFC->Type(Ordered_parameter_assignment) ==
               VObjectType::paOrdered_parameter_assignment)) {
            while (Ordered_parameter_assignment) {
              NodeId Param_expression =
                  actualFC->Child(Ordered_parameter_assignment);
              NodeId Data_type = actualFC->Child(Param_expression);
              std::string fName;
              const DesignComponent::ParameterVec& formal =
                  classDefn->getOrderedParameters();
              uhdm::Any* fparam = nullptr;
              if (index < formal.size()) {
                Parameter* p = formal.at(index);
                fName = p->getName();
                fparam = p->getUhdmParam();

                if (actualFC->Type(Data_type) == VObjectType::paData_type) {
                  uhdm::Typespec* tps = compileTypespec(
                      component, actualFC, Data_type, InvalidNodeId,
                      compileDesign, reduce, result, instance, false);

                  uhdm::TypeParameter* tp = s.make<uhdm::TypeParameter>();
                  tp->setName(fName);
                  tp->setParent(ref);
                  tps->setParent(tp);
                  uhdm::RefTypespec* tpsRef = s.make<uhdm::RefTypespec>();
                  tpsRef->setParent(tp);
                  tpsRef->setName(fName);
                  tpsRef->setActual(tps);
                  tp->setTypespec(tpsRef);
                  p->getFileContent()->populateCoreMembers(p->getNodeId(),
                                                           p->getNodeId(), tp);
                  p->getFileContent()->populateCoreMembers(
                      p->getNodeId(), p->getNodeId(), tpsRef);
                  params->emplace_back(tp);
                  uhdm::ParamAssign* pass = s.make<uhdm::ParamAssign>();
                  pass->setRhs(tp);
                  pass->setLhs(fparam);
                  pass->setParent(ref);
                  pass->setFile(fparam->getFile());
                  pass->setStartLine(fparam->getStartLine());
                  pass->setStartColumn(fparam->getStartColumn());
                  pass->setEndLine(tp->getEndLine());
                  pass->setEndColumn(tp->getEndColumn());
                  assigns->emplace_back(pass);
                } else if (uhdm::Any* exp = compileExpression(
                               component, actualFC, Param_expression,
                               compileDesign, reduce, nullptr, instance)) {
                  if (exp->getUhdmType() == uhdm::UhdmType::RefObj) {
                    const std::string_view name =
                        ((uhdm::RefObj*)exp)->getName();
                    if (uhdm::Typespec* tps = compileDatastructureTypespec(
                            component, actualFC, param, compileDesign, reduce,
                            instance, "", name)) {
                      uhdm::TypeParameter* tp = s.make<uhdm::TypeParameter>();
                      tp->setName(fName);
                      uhdm::RefTypespec* tpsRef = s.make<uhdm::RefTypespec>();
                      tpsRef->setParent(tp);
                      tpsRef->setName(name);
                      tpsRef->setActual(tps);
                      p->getFileContent()->populateCoreMembers(
                          p->getNodeId(), p->getNodeId(), tp);
                      p->getFileContent()->populateCoreMembers(
                          p->getNodeId(), p->getNodeId(), tpsRef);
                      tp->setTypespec(tpsRef);
                      tps->setParent(tp);
                      tp->setParent(ref);
                      params->emplace_back(tp);
                      uhdm::ParamAssign* pass = s.make<uhdm::ParamAssign>();
                      pass->setRhs(tp);
                      pass->setLhs(fparam);
                      pass->setParent(ref);
                      pass->setFile(fparam->getFile());
                      pass->setStartLine(fparam->getStartLine());
                      pass->setStartColumn(fparam->getStartColumn());
                      pass->setEndLine(tp->getEndLine());
                      pass->setEndColumn(tp->getEndColumn());
                      fC->populateCoreMembers(InvalidNodeId, InvalidNodeId,
                                              pass);
                      assigns->emplace_back(pass);
                    }
                  }
                }
              }
              Ordered_parameter_assignment =
                  actualFC->Sibling(Ordered_parameter_assignment);
              index++;
            }
          }
        }
        break;
      }
      // if (result)
      //  break;
      dt = dt->getDefinition();
    }

    if (result == nullptr) {
      const std::string_view libName = fC->getLibrary()->getName();
      ModuleDefinition* def =
          design->getModuleDefinition(StrCat(libName, "@", typeName));
      if (def) {
        if (def->getType() == VObjectType::paInterface_declaration) {
          uhdm::InterfaceTypespec* tps = s.make<uhdm::InterfaceTypespec>();
          tps->setName(typeName);
          tps->setInterface(def->getUhdmModel<uhdm::Interface>());
          fC->populateCoreMembers(type, type, tps);
          result = tps;
          if (!suffixname.empty()) {
            const DataType* defType = def->getDataType(design, suffixname);
            bool foundDataType = false;
            while (defType) {
              foundDataType = true;
              if (uhdm::Typespec* t = defType->getTypespec()) {
                result = t;
                return result;
              }
              defType = defType->getDefinition();
            }
            if (foundDataType) {
              // The binding to the actual typespec is still incomplete
              result = s.make<uhdm::LogicTypespec>();
              return result;
            }
          }
          if (NodeId sub = fC->Sibling(type)) {
            const std::string_view name = fC->SymName(sub);
            if (def->getModport(name)) {
              uhdm::InterfaceTypespec* mptps =
                  s.make<uhdm::InterfaceTypespec>();
              mptps->setName(name);
              mptps->setInterface(def->getUhdmModel<uhdm::Interface>());
              fC->populateCoreMembers(sub, sub, mptps);
              mptps->setParent(tps);
              mptps->setIsModport(true);
              result = mptps;
            }
          }
        }
      }
    }

    if (result == nullptr) {
      uhdm::UnsupportedTypespec* tps = s.make<uhdm::UnsupportedTypespec>();
      tps->setName(typeName);
      tps->setParent(pscope);
      fC->populateCoreMembers(type, type, tps);
      result = tps;
    }
  } else {
    uhdm::UnsupportedTypespec* tps = s.make<uhdm::UnsupportedTypespec>();
    tps->setName(typeName);
    tps->setParent(pscope);
    fC->populateCoreMembers(type, type, tps);
    result = tps;
  }
  return result;
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
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Int: {
      uhdm::IntTypespec* var = s.make<uhdm::IntTypespec>();
      var->setSigned(isSigned);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Integer: {
      uhdm::IntegerTypespec* var = s.make<uhdm::IntegerTypespec>();
      var->setSigned(isSigned);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Byte: {
      uhdm::ByteTypespec* var = s.make<uhdm::ByteTypespec>();
      var->setSigned(isSigned);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_LongInt: {
      uhdm::LongIntTypespec* var = s.make<uhdm::LongIntTypespec>();
      var->setSigned(isSigned);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Shortint: {
      uhdm::ShortIntTypespec* var = s.make<uhdm::ShortIntTypespec>();
      var->setSigned(isSigned);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Time: {
      uhdm::TimeTypespec* var = s.make<uhdm::TimeTypespec>();
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
      result = var;
      break;
    }
    case VObjectType::paNonIntType_ShortReal: {
      uhdm::ShortRealTypespec* var = s.make<uhdm::ShortRealTypespec>();
      result = var;
      break;
    }
    case VObjectType::paNonIntType_Real: {
      uhdm::RealTypespec* var = s.make<uhdm::RealTypespec>();
      result = var;
      break;
    }
    case VObjectType::paString_type: {
      uhdm::StringTypespec* tps = s.make<uhdm::StringTypespec>();
      result = tps;
      break;
    }
    default:
      uhdm::LogicTypespec* var = s.make<uhdm::LogicTypespec>();
      result = var;
      break;
  }
  result->setParent(pstmt);
  return result;
}

uhdm::Typespec* CompileHelper::compileUpdatedTypespec(
    DesignComponent* component, const FileContent* fC, NodeId nodeId,
    NodeId packedId, NodeId unpackedId, CompileDesign* compileDesign,
    Reduce reduce, uhdm::Any* pstmt, uhdm::Typespec* ts) {
  if (!packedId && !unpackedId) return ts;

  uhdm::Typespec* retts = ts;
  uhdm::Serializer& s = compileDesign->getSerializer();
  Design* const design = compileDesign->getCompiler()->getDesign();
  uhdm::Any* pscope = component->getUhdmModel();
  if (pscope == nullptr) pscope = design->getUhdmDesign();

  std::vector<uhdm::Range*>* packedDimensions = nullptr;
  if (packedId) {
    int32_t packedSize = 0;
    packedDimensions =
        compileRanges(component, fC, packedId, compileDesign, Reduce::Yes,
                      nullptr, nullptr, packedSize, false);
  }

  std::vector<uhdm::Range*>* unpackedDimensions = nullptr;
  if (unpackedId) {
    int32_t unpackedSize = 0;
    unpackedDimensions =
        compileRanges(component, fC, unpackedId, compileDesign, Reduce::Yes,
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

  bool dynamic = false;
  bool associative = false;
  bool queue = false;
  if (packedId && unpackedId) {
    uhdm::ArrayTypespec* taps = s.make<uhdm::ArrayTypespec>();
    NodeId unpackDimensionId2 = unpackedId;
    while (fC->Sibling(unpackDimensionId2)) {
      unpackDimensionId2 = fC->Sibling(unpackDimensionId2);
    }

    // create packed array
    uhdm::PackedArrayTypespec* tpaps = s.make<uhdm::PackedArrayTypespec>();
    tpaps->setParent(pscope);
    uhdm::RefTypespec* pret = s.make<uhdm::RefTypespec>();
    pret->setParent(taps);
    pret->setActual(tpaps);
    taps->setElemTypespec(pret);
    fC->populateCoreMembers(nodeId, nodeId, pret);

    uhdm::RefTypespec* ert = s.make<uhdm::RefTypespec>();
    ert->setParent(tpaps);
    ert->setActual(ts);
    ert->setName(ts->getName());
    tpaps->setElemTypespec(ert);
    fC->populateCoreMembers(packedId, packedId, ert);

    // Associative array
    for (auto itr = unpackedDimensions->begin();
         itr != unpackedDimensions->end(); ++itr) {
      uhdm::Range* r = *itr;
      const uhdm::Expr* rhs = r->getRightExpr();
      if (rhs->getUhdmType() == uhdm::UhdmType::Constant) {
        const std::string_view value = rhs->getValue();
        if (value == "STRING:$") {
          queue = true;
          unpackedDimensions->erase(itr);
          break;
        } else if (value == "STRING:associative") {
          associative = true;
          const uhdm::Typespec* tp = nullptr;
          if (const uhdm::RefTypespec* rt = rhs->getTypespec()) {
            tp = rt->getActual();
          }

          if (tp != nullptr) {
            uhdm::RefTypespec* tpRef = s.make<uhdm::RefTypespec>();
            tpRef->setParent(taps);
            tpRef->setName(tp->getName());
            taps->setIndexTypespec(tpRef);
            fC->populateCoreMembers(nodeId, unpackedId, tpRef);
          }
          taps->getIndexTypespec()->setActual(const_cast<uhdm::Typespec*>(tp));

          unpackedDimensions->erase(itr);
          break;
        } else if (value == "STRING:unsized") {
          dynamic = true;
          unpackedDimensions->erase(itr);
          break;
        }
      }
    }
    if (associative)
      taps->setArrayType(vpiAssocArray);
    else if (queue)
      taps->setArrayType(vpiQueueArray);
    else if (dynamic)
      taps->setArrayType(vpiDynamicArray);
    else
      taps->setArrayType(vpiStaticArray);

    tpaps->setRanges(packedDimensions);
    for (auto r : *packedDimensions) r->setParent(tpaps);

    taps->setRanges(unpackedDimensions);
    for (auto r : *unpackedDimensions) r->setParent(taps);

    retts = taps;

  } else if (unpackedId) {
    uhdm::ArrayTypespec* taps = s.make<uhdm::ArrayTypespec>();
    taps->setParent(pscope);
    NodeId unpackDimensionId2 = unpackedId;
    while (fC->Sibling(unpackDimensionId2)) {
      unpackDimensionId2 = fC->Sibling(unpackDimensionId2);
    }

    uhdm::RefTypespec* ert = s.make<uhdm::RefTypespec>();
    ert->setParent(taps);
    ert->setActual(ts);
    ert->setName(ts->getName());
    taps->setElemTypespec(ert);
    fC->populateCoreMembers(nodeId, nodeId, ert);

    // Associative array
    for (auto itr = unpackedDimensions->begin();
         itr != unpackedDimensions->end(); ++itr) {
      uhdm::Range* r = *itr;
      const uhdm::Expr* rhs = r->getRightExpr();
      if (rhs->getUhdmType() == uhdm::UhdmType::Constant) {
        const std::string_view value = rhs->getValue();
        if (value == "STRING:$") {
          queue = true;
          unpackedDimensions->erase(itr);
          break;
        } else if (value == "STRING:associative") {
          associative = true;
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
            fC->populateCoreMembers(unpackedId, unpackedId, tpRef);
          }

          unpackedDimensions->erase(itr);
          break;
        } else if (value == "STRING:unsized") {
          dynamic = true;
          unpackedDimensions->erase(itr);
          break;
        }
      }
    }
    if (associative)
      taps->setArrayType(vpiAssocArray);
    else if (queue)
      taps->setArrayType(vpiQueueArray);
    else if (dynamic)
      taps->setArrayType(vpiDynamicArray);
    else
      taps->setArrayType(vpiStaticArray);

    taps->setRanges(unpackedDimensions);
    for (auto r : *unpackedDimensions) r->setParent(taps);
    retts = taps;

  } else if (packedId) {
    uhdm::PackedArrayTypespec* taps = s.make<uhdm::PackedArrayTypespec>();
    taps->setParent(pscope);

    if (ts != nullptr) {
      uhdm::RefTypespec* ert = s.make<uhdm::RefTypespec>();
      ert->setParent(taps);
      ert->setActual(ts);
      taps->setElemTypespec(ert);
      ert->setName(ts->getName());
      ert->setFile(packedDimensions->front()->getFile());
      ert->setStartLine(packedDimensions->front()->getStartLine());
      ert->setStartColumn(packedDimensions->front()->getStartColumn());
      ert->setEndLine(packedDimensions->back()->getEndLine());
      ert->setEndColumn(packedDimensions->back()->getEndColumn());
    }

    taps->setRanges(packedDimensions);
    for (uhdm::Range* r : *packedDimensions) r->setParent(taps, true);
    retts = taps;
  }
  return retts;
}

uhdm::Typespec* CompileHelper::compileTypespec(
    DesignComponent* component, const FileContent* fC, NodeId id,
    NodeId unpackedDimId, CompileDesign* compileDesign, Reduce reduce,
    uhdm::Any* pstmt, ValuedComponentI* instance, bool isVariable) {
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
  if ((fC->Type(unpackedDimId) != VObjectType::paUnpacked_dimension) &&
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
                               compileDesign, reduce, pstmt, instance, false);
      break;
    }
    case VObjectType::paSystem_task: {
      if (uhdm::Any* res = compileExpression(component, fC, type, compileDesign,
                                             reduce, pstmt, instance)) {
        uhdm::IntegerTypespec* var = s.make<uhdm::IntegerTypespec>();
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
                            compileDesign, reduce, pstmt, instance, isVariable);

        type = fC->Sibling(type);
      }
      result = baseType;
      break;
    }
    case VObjectType::paInterface_identifier: {
      uhdm::InterfaceTypespec* tps = s.make<uhdm::InterfaceTypespec>();
      const std::string_view name = fC->SymName(type);
      tps->setName(name);
      fC->populateCoreMembers(type, type, tps);
      const std::string_view libName = fC->getLibrary()->getName();
      if (ModuleDefinition* const def =
              design->getModuleDefinition(StrCat(libName, "@", name))) {
        tps->setInterface(def->getUhdmModel<uhdm::Interface>());
      }
      result = tps;
      break;
    }
    case VObjectType::paSigning_Signed: {
      if (m_elaborate == Elaborate::Yes) {
        if (isVariable) {
          // 6.8 Variable declarations, implicit type
          uhdm::LogicTypespec* tps = s.make<uhdm::LogicTypespec>();
          tps->setSigned(true);
          result = tps;
        } else {
          // Parameter implicit type is int
          uhdm::IntTypespec* tps = s.make<uhdm::IntTypespec>();
          tps->setSigned(true);
          result = tps;
        }
      }
      break;
    }
    case VObjectType::paSigning_Unsigned: {
      if (isVariable) {
        // 6.8 Variable declarations, implicit type
        uhdm::LogicTypespec* tps = s.make<uhdm::LogicTypespec>();
        result = tps;
      } else {
        // Parameter implicit type is int
        uhdm::IntTypespec* tps = s.make<uhdm::IntTypespec>();
        result = tps;
      }
      break;
    }
    case VObjectType::paPacked_dimension: {
      if (isVariable) {
        // 6.8 Variable declarations, implicit type
        uhdm::LogicTypespec* tps = s.make<uhdm::LogicTypespec>();
        result = tps;
      } else {
        // Parameter implicit type is bit
        uhdm::IntTypespec* tps = s.make<uhdm::IntTypespec>();
        result = tps;
      }
      break;
    }
    case VObjectType::paExpression: {
      NodeId Primary = fC->Child(type);
      NodeId Primary_literal = fC->Child(Primary);
      NodeId Name = fC->Child(Primary_literal);
      if (fC->Type(Name) == VObjectType::paClass_scope) {
        result =
            compileTypespec(component, fC, Name, InvalidNodeId, compileDesign,
                            reduce, pstmt, instance, isVariable);
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
            component, fC, type, compileDesign, reduce, instance, "", typeName);
      } else {
        uhdm::IntegerTypespec* var = s.make<uhdm::IntegerTypespec>();
        var->setValue(StrCat("INT:", fC->SymName(literal)));
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
          if (dtype) {
            uhdm::ClassTypespec* ref = s.make<uhdm::ClassTypespec>();
            ref->setClassDefn(classDefn->getUhdmModel<uhdm::ClassDefn>());
            ref->setName(typeName);
            fC->populateCoreMembers(type, type, ref);
            result = ref;
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
        fC->populateCoreMembers(id, id, ref);
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
            member_ts =
                compileTypespec(component, fC, Data_type, tmpUnpackedId,
                                compileDesign, reduce, result, instance, false);
          } else {
            uhdm::VoidTypespec* tps = s.make<uhdm::VoidTypespec>();
            tps->setParent(result);
            member_ts = tps;
          }

          uhdm::TypespecMember* m =
              buildTypespecMember(compileDesign, fC, member_name);
          m->setParent(structOtUnionTypespec);
          if (member_ts != nullptr) {
            if (m->getTypespec() == nullptr) {
              uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
              tsRef->setParent(m);
              tsRef->setName(fC->SymName(Data_type));
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
                                      reduce, m, instance, false)) {
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
                               compileDesign, reduce, pstmt, instance, false);
      break;
    }
    case VObjectType::STRING_CONST: {
      const std::string_view typeName = fC->SymName(type);
      if (typeName == "logic") {
        uhdm::LogicTypespec* var = s.make<uhdm::LogicTypespec>();
        result = var;
      } else if (typeName == "bit") {
        uhdm::BitTypespec* var = s.make<uhdm::BitTypespec>();
        result = var;
      } else if (typeName == "byte") {
        uhdm::ByteTypespec* var = s.make<uhdm::ByteTypespec>();
        result = var;
      } else if ((m_reduce == Reduce::Yes) && (reduce == Reduce::Yes)) {
        if (uhdm::Any* cast_to =
                getValue(typeName, component, compileDesign,
                         reduce == Reduce::Yes ? Reduce::No : Reduce::Yes,
                         instance, fC->getFileId(), fC->Line(type), nullptr)) {
          uhdm::Constant* c = any_cast<uhdm::Constant>(cast_to);
          if (c) {
            uhdm::IntegerTypespec* var = s.make<uhdm::IntegerTypespec>();
            var->setValue(c->getValue());
            result = var;
          } else {
            uhdm::VoidTypespec* tps = s.make<uhdm::VoidTypespec>();
            result = tps;
          }
        }
      }
      if (!result) {
        while (instance) {
          if (ModuleInstance* inst =
                  valuedcomponenti_cast<ModuleInstance*>(instance)) {
            if (inst->getNetlist()) {
              uhdm::ParamAssignCollection* param_assigns =
                  inst->getNetlist()->param_assigns();
              if (param_assigns) {
                for (uhdm::ParamAssign* param : *param_assigns) {
                  const std::string_view param_name =
                      param->getLhs()->getName();
                  if (param_name == typeName) {
                    const uhdm::Any* rhs = param->getRhs();
                    if (const uhdm::Constant* exp =
                            any_cast<const uhdm::Constant*>(rhs)) {
                      uhdm::IntTypespec* its = buildIntTypespec(
                          compileDesign,
                          fileSystem->toPathId(param->getFile(), symbols),
                          typeName, exp->getValue(), param->getStartLine(),
                          param->getStartColumn(), param->getStartLine(),
                          param->getStartColumn());
                      result = its;
                    } else {
                      uhdm::Any* ex =
                          compileExpression(component, fC, type, compileDesign,
                                            Reduce::No, pstmt, instance, false);
                      if (ex) {
                        uhdm::HierPath* path = nullptr;
                        if (ex->getUhdmType() == uhdm::UhdmType::HierPath) {
                          path = (uhdm::HierPath*)ex;
                        } else if (ex->getUhdmType() ==
                                   uhdm::UhdmType::RefObj) {
                          path = s.make<uhdm::HierPath>();
                          uhdm::RefObj* ref = s.make<uhdm::RefObj>();
                          ref->setName(typeName);
                          ref->setParent(path);
                          fC->populateCoreMembers(type, type, ref);
                          path->getPathElems(true)->emplace_back(ref);
                        }
                        if (path) {
                          bool invalidValue = false;
                          result = (uhdm::Typespec*)decodeHierPath(
                              path, invalidValue, component, compileDesign,
                              reduce, instance, fC->getFileId(), fC->Line(type),
                              nullptr, false, true);
                        }
                      }
                    }
                    break;
                  }
                }
              }
            }
          }
          instance = (ValuedComponentI*)instance->getParentScope();
        }
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
            uhdm::ClassTypespec* tps = s.make<uhdm::ClassTypespec>();
            tps->setName(typeName);
            tps->setParent(pstmt);
            tps->setClassDefn(cl->getUhdmModel<uhdm::ClassDefn>());

            NodeId endType = type;
            if (const NodeId Parameter_value_assignment = fC->Sibling(type)) {
              if (uhdm::ParamAssignCollection* const paramAssigns =
                      compileParameterValueAssignments(
                          component, fC, compileDesign,
                          Parameter_value_assignment, tps, cl)) {
                endType = Parameter_value_assignment;
                tps->setParamAssigns(paramAssigns);

                uhdm::AnyCollection* const params = tps->getParameters(true);
                for (uhdm::ParamAssign* pa : *paramAssigns) {
                  params->emplace_back(pa->getLhs());
                }
              }
            }
            fC->populateCoreMembers(type, endType, tps);
            result = tps;
          }
        }
      }
      if (result == nullptr) {
        result = compileDatastructureTypespec(
            component, fC, type, compileDesign, reduce, instance, "", typeName);
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
              component, fC, type, compileDesign, reduce, pstmt, instance,
              reduce == Reduce::No)) {
        if (exp->getUhdmType() == uhdm::UhdmType::RefObj) {
          result =
              compileTypespec(component, fC, fC->Child(type), InvalidNodeId,
                              compileDesign, reduce, result, instance, false);
        } else {
          uhdm::IntegerTypespec* var = s.make<uhdm::IntegerTypespec>();
          if (exp->getUhdmType() == uhdm::UhdmType::Constant) {
            var->setValue(exp->getValue());
          } else {
            var->setExpr(exp);
            exp->setParent(var, true);
          }
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
      if (uhdm::RangeCollection* ranges =
              compileRanges(component, fC, type, compileDesign, reduce, tps,
                            instance, size, false)) {
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
      result = tps;
      break;
    }
    case VObjectType::paNonIntType_RealTime: {
      uhdm::TimeTypespec* tps = s.make<uhdm::TimeTypespec>();
      result = tps;
      break;
    }
    case VObjectType::paType_reference: {
      NodeId child = fC->Child(type);
      if (fC->Type(child) == VObjectType::paExpression) {
        uhdm::Expr* exp = (uhdm::Expr*)compileExpression(
            component, fC, child, compileDesign, reduce, nullptr, instance,
            reduce == Reduce::Yes);
        if (exp) {
          uhdm::UhdmType typ = exp->getUhdmType();
          if (typ == uhdm::UhdmType::RefObj) {
            result =
                compileTypespec(component, fC, child, InvalidNodeId,
                                compileDesign, reduce, result, instance, false);
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
        result =
            compileTypespec(component, fC, child, InvalidNodeId, compileDesign,
                            reduce, result, instance, false);
      }
      break;
    }
    case VObjectType::paData_type_or_implicit: {
      uhdm::LogicTypespec* tps = s.make<uhdm::LogicTypespec>();
      result = tps;
      break;
    }
    case VObjectType::paImplicit_data_type: {
      // Interconnect
      uhdm::LogicTypespec* tps = s.make<uhdm::LogicTypespec>();
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
    result = compileUpdatedTypespec(component, fC, id, Packed_dimension,
                                    unpackedDimId, compileDesign, reduce, pstmt,
                                    result);
    if (result != nullptr) {
      if ((m_elaborate == Elaborate::Yes) && component &&
          !result->getInstance()) {
        result->setInstance(component->getUhdmModel<uhdm::Instance>());
      }
      result->setParent(pstmt);
    }
  }

  return result;
}

uhdm::Typespec* CompileHelper::elabTypespec(DesignComponent* component,
                                            uhdm::Typespec* spec,
                                            CompileDesign* compileDesign,
                                            uhdm::Any* pexpr,
                                            ValuedComponentI* instance) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();

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
    case uhdm::UhdmType::PackedArrayTypespec: {
      uhdm::PackedArrayTypespec* tps = (uhdm::PackedArrayTypespec*)spec;
      ranges = tps->getRanges();
      if (ranges) {
        uhdm::ElaboratorContext elaboratorContext(&s, false, true);
        uhdm::PackedArrayTypespec* res = any_cast<uhdm::PackedArrayTypespec>(
            uhdm::clone_tree((uhdm::Any*)spec, &elaboratorContext));
        ranges = res->getRanges();
        result = res;
      }
      break;
    }
    default:
      break;
  }
  if ((m_reduce == Reduce::Yes) && ranges) {
    for (uhdm::Range* oldRange : *ranges) {
      uhdm::Expr* oldLeft = oldRange->getLeftExpr();
      uhdm::Expr* oldRight = oldRange->getRightExpr();
      bool invalidValue = false;
      uhdm::Expr* newLeft =
          reduceExpr(oldLeft, invalidValue, component, compileDesign, instance,
                     fileSystem->toPathId(oldLeft->getFile(), symbols),
                     oldLeft->getStartLine(), pexpr);
      uhdm::Expr* newRight =
          reduceExpr(oldRight, invalidValue, component, compileDesign, instance,
                     fileSystem->toPathId(oldRight->getFile(), symbols),
                     oldRight->getStartLine(), pexpr);
      if (!invalidValue) {
        oldRange->setLeftExpr(newLeft);
        oldRange->setRightExpr(newRight);
      }
    }
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
      case uhdm::UhdmType::PackedArrayTypespec: {
        uhdm::PackedArrayTypespec* tps = (uhdm::PackedArrayTypespec*)tmp;
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
      case uhdm::UhdmType::Parameter:
      case uhdm::UhdmType::RefObj:
      case uhdm::UhdmType::TypeParameter: {
        if (inst->isOverridenParam(tmp->getName())) return true;
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

uhdm::ParamAssignCollection* CompileHelper::compileParameterValueAssignments(
    DesignComponent* component, const FileContent* fC,
    CompileDesign* compileDesign, NodeId id, uhdm::Any* pstmt,
    DesignComponent* classComponent) {
  if (!id) return nullptr;

  NodeId Parameter_assignment_list = fC->Child(id);
  NodeId Ordered_parameter_assignment = fC->Child(Parameter_assignment_list);
  if (!Ordered_parameter_assignment) return nullptr;

  uhdm::Serializer& s = compileDesign->getSerializer();
  const DesignComponent::ParameterVec& formals =
      classComponent->getOrderedParameters();
  uhdm::ParamAssignCollection* assigns = nullptr;

  size_t index = 0;
  while (Ordered_parameter_assignment && (index < formals.size())) {
    NodeId Param_expression = fC->Child(Ordered_parameter_assignment);
    NodeId Data_type = fC->Child(Param_expression);

    Parameter* p = formals[index];
    std::string_view fName = p->getName();
    uhdm::Any* const fparam = p->getUhdmParam();

    if (fC->Type(Data_type) == VObjectType::paData_type) {
      uhdm::Typespec* tps =
          compileTypespec(component, fC, Data_type, InvalidNodeId,
                          compileDesign, Reduce::No, pstmt, nullptr, false);

      uhdm::ParamAssign* pass = s.make<uhdm::ParamAssign>();
      pass->setRhs(tps);
      pass->setLhs(fparam);
      pass->setParent(pstmt);
      pass->setStartLine(tps->getStartLine());
      pass->setStartColumn(tps->getStartColumn());
      pass->setEndLine(tps->getEndLine());
      pass->setEndColumn(tps->getEndColumn());
      fC->populateCoreMembers(Data_type, Data_type, pass);

      if (assigns == nullptr) {
        assigns = s.makeCollection<uhdm::ParamAssign>();
      }
      assigns->emplace_back(pass);
    } else if (uhdm::Any* exp = compileExpression(
                   component, fC, Param_expression, compileDesign, Reduce::No,
                   nullptr, nullptr)) {
      if (exp->getUhdmType() == uhdm::UhdmType::RefObj) {
        const std::string_view name = exp->getName();
        if (uhdm::Typespec* const tps = compileDatastructureTypespec(
                component, fC, Parameter_assignment_list, compileDesign,
                Reduce::No, nullptr, "", name)) {
          uhdm::ParamAssign* pass = s.make<uhdm::ParamAssign>();
          pass->setRhs(tps);
          pass->setLhs(fparam);
          pass->setParent(pstmt);
          pass->setStartLine(tps->getStartLine());
          pass->setStartColumn(tps->getStartColumn());
          pass->setEndLine(tps->getEndLine());
          pass->setEndColumn(tps->getEndColumn());
          fC->populateCoreMembers(Parameter_assignment_list,
                                  Parameter_assignment_list, pass);

          if (assigns == nullptr) {
            assigns = s.makeCollection<uhdm::ParamAssign>();
          }
          assigns->emplace_back(pass);
        }
      }
    }

    ++index;
    Ordered_parameter_assignment = fC->Sibling(Ordered_parameter_assignment);
  }
  return assigns;
}
}  // namespace SURELOG
