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

#include <Surelog/CommandLine/CommandLineParser.h>
#include <Surelog/Common/FileSystem.h>
#include <Surelog/Common/Session.h>
#include <Surelog/Design/DataType.h>
#include <Surelog/Design/DummyType.h>
#include <Surelog/Design/Enum.h>
#include <Surelog/Design/FileContent.h>
#include <Surelog/Design/Function.h>
#include <Surelog/Design/ModuleDefinition.h>
#include <Surelog/Design/ModuleInstance.h>
#include <Surelog/Design/Netlist.h>
#include <Surelog/Design/ParamAssign.h>
#include <Surelog/Design/Parameter.h>
#include <Surelog/Design/SimpleType.h>
#include <Surelog/Design/Struct.h>
#include <Surelog/Design/Task.h>
#include <Surelog/Design/Union.h>
#include <Surelog/DesignCompile/CompileDesign.h>
#include <Surelog/DesignCompile/CompileHelper.h>
#include <Surelog/Library/Library.h>
#include <Surelog/Package/Package.h>
#include <Surelog/SourceCompile/Compiler.h>
#include <Surelog/SourceCompile/SymbolTable.h>
#include <Surelog/Testbench/ClassDefinition.h>
#include <Surelog/Testbench/TypeDef.h>
#include <Surelog/Testbench/Variable.h>
#include <Surelog/Utils/StringUtils.h>

// UHDM
#include <uhdm/ElaboratorListener.h>
#include <uhdm/ExprEval.h>
#include <uhdm/clone_tree.h>
#include <uhdm/uhdm.h>

#include <stack>

namespace SURELOG {

using namespace UHDM;  // NOLINT (using a bunch of them)

variables* CompileHelper::getSimpleVarFromTypespec(
    const FileContent* fC, NodeId declarationId, NodeId nameId,
    UHDM::typespec* spec, std::vector<UHDM::range*>* packedDimensions,
    CompileDesign* compileDesign) {
  Serializer& s = compileDesign->getSerializer();
  variables* var = nullptr;
  UHDM_OBJECT_TYPE ttps = spec->UhdmType();
  switch (ttps) {
    case uhdmint_typespec: {
      UHDM::int_var* int_var = s.MakeInt_var();
      var = int_var;
      break;
    }
    case uhdminteger_typespec: {
      UHDM::integer_var* integer_var = s.MakeInteger_var();
      var = integer_var;
      break;
    }
    case uhdmlong_int_typespec: {
      UHDM::long_int_var* int_var = s.MakeLong_int_var();
      var = int_var;
      break;
    }
    case uhdmstring_typespec: {
      UHDM::string_var* int_var = s.MakeString_var();
      var = int_var;
      break;
    }
    case uhdmshort_int_typespec: {
      UHDM::short_int_var* int_var = s.MakeShort_int_var();
      var = int_var;
      break;
    }
    case uhdmbyte_typespec: {
      UHDM::byte_var* int_var = s.MakeByte_var();
      var = int_var;
      break;
    }
    case uhdmreal_typespec: {
      UHDM::real_var* int_var = s.MakeReal_var();
      var = int_var;
      break;
    }
    case uhdmshort_real_typespec: {
      UHDM::short_real_var* int_var = s.MakeShort_real_var();
      var = int_var;
      break;
    }
    case uhdmtime_typespec: {
      UHDM::time_var* int_var = s.MakeTime_var();
      var = int_var;
      break;
    }
    case uhdmbit_typespec: {
      UHDM::bit_var* int_var = s.MakeBit_var();
      var = int_var;
      break;
    }
    case uhdmclass_typespec: {
      UHDM::class_var* int_var = s.MakeClass_var();
      var = int_var;
      break;
    }
    case uhdmenum_typespec: {
      UHDM::enum_var* enumv = s.MakeEnum_var();
      fC->populateCoreMembers(nameId, nameId, enumv);
      var = enumv;
      if (m_elaborate == Elaborate::Yes) {
        ref_typespec* specRef = s.MakeRef_typespec();
        specRef->VpiParent(enumv);
        specRef->Actual_typespec(spec);
        enumv->Typespec(specRef);
      }
      if (packedDimensions != nullptr) {
        packed_array_var* array = s.MakePacked_array_var();
        array->Ranges(packedDimensions);
        for (auto r : *packedDimensions) r->VpiParent(array, true);
        array->Elements(true)->push_back(var);
        var->VpiParent(array);
        var = array;
      }
      break;
    }
    case uhdmlogic_typespec: {
      logic_var* logicv = s.MakeLogic_var();
      fC->populateCoreMembers(nameId, nameId, logicv);
      var = logicv;

      if (packedDimensions != nullptr) {
        packed_array_var* array = s.MakePacked_array_var();
        array->Ranges(packedDimensions);
        for (auto r : *packedDimensions) r->VpiParent(array, true);
        array->Elements(true)->push_back(var);
        var->VpiParent(array);
        var = array;
      }

      break;
    }
    case uhdmvoid_typespec: {
      logic_var* logicv = s.MakeLogic_var();
      var = logicv;
      break;
    }
    case uhdmunion_typespec: {
      UHDM::union_var* unionv = s.MakeUnion_var();
      fC->populateCoreMembers(nameId, nameId, unionv);
      var = unionv;

      if (m_elaborate == Elaborate::Yes) {
        ref_typespec* specRef = s.MakeRef_typespec();
        specRef->VpiParent(var);
        specRef->Actual_typespec(spec);
        var->Typespec(specRef);
      }
      if (packedDimensions != nullptr) {
        packed_array_var* array = s.MakePacked_array_var();
        for (auto pd : *packedDimensions) pd->VpiParent(array, true);
        array->Ranges(packedDimensions);
        array->Elements(true)->push_back(var);
        var->VpiParent(array);
        var = array;
      }
      break;
    }
    case uhdmstruct_typespec: {
      UHDM::struct_var* structv = s.MakeStruct_var();
      fC->populateCoreMembers(nameId, nameId, structv);
      var = structv;

      if (m_elaborate == Elaborate::Yes) {
        ref_typespec* specRef = s.MakeRef_typespec();
        specRef->VpiParent(var);
        specRef->Actual_typespec(spec);
        var->Typespec(specRef);
      }
      if (packedDimensions != nullptr) {
        packed_array_var* array = s.MakePacked_array_var();
        for (auto pd : *packedDimensions) pd->VpiParent(array, true);
        array->Ranges(packedDimensions);
        array->Elements(true)->push_back(var);
        var->VpiParent(array);
        var = array;
      }
      break;
    }
    case uhdmarray_typespec: {
      array_typespec* atps = (array_typespec*)spec;
      if (ref_typespec* atps_rt = atps->Index_typespec()) {
        if (typespec* indextps = atps_rt->Actual_typespec()) {
          return getSimpleVarFromTypespec(fC, declarationId, nameId, indextps,
                                          packedDimensions, compileDesign);
        }
      } else {
        UHDM::array_var* array = s.MakeArray_var();
        if (m_elaborate == Elaborate::Yes) {
          ref_typespec* tpsRef = s.MakeRef_typespec();
          tpsRef->VpiParent(array);
          tpsRef->Actual_typespec(s.MakeArray_typespec());
          array->Typespec(tpsRef);
        }
        var = array;
      }
      break;
    }
    default:
      break;
  }
  if (var && (m_elaborate == Elaborate::Yes)) {
    if (var->Typespec() == nullptr) {
      ref_typespec* specRef = s.MakeRef_typespec();
      specRef->VpiParent(var);
      var->Typespec(specRef);
    }
    var->Typespec()->Actual_typespec(spec);
  }
  return var;
}

UHDM::any* CompileHelper::compileVariable(
    DesignComponent* component, const FileContent* fC, NodeId declarationId,
    NodeId nameId, CompileDesign* compileDesign, Reduce reduce,
    UHDM::any* pstmt, SURELOG::ValuedComponentI* instance, bool muteErrors) {
  UHDM::Serializer& s = compileDesign->getSerializer();
  Design* design = compileDesign->getCompiler()->getDesign();
  UHDM::any* result = nullptr;
  NodeId variable = declarationId;
  VObjectType the_type = fC->Type(variable);
  if (the_type == VObjectType::paData_type ||
      the_type == VObjectType::paPs_or_hierarchical_identifier) {
    variable = fC->Child(variable);
    the_type = fC->Type(variable);
    if (the_type == VObjectType::paVIRTUAL) {
      variable = fC->Sibling(variable);
      the_type = fC->Type(variable);
    }
  } else if (the_type == VObjectType::paImplicit_class_handle) {
    NodeId Handle = fC->Child(variable);
    if (fC->Type(Handle) == VObjectType::paThis_keyword) {
      variable = fC->Sibling(variable);
      the_type = fC->Type(variable);
    }
  } else if (the_type == VObjectType::sl_INVALID_) {
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

  if (fC->Type(variable) == VObjectType::slStringConst &&
      fC->Type(Packed_dimension) == VObjectType::slStringConst) {
    UHDM::hier_path* path = s.MakeHier_path();
    VectorOfany* elems = path->Path_elems(true);
    std::string fullName(fC->SymName(variable));
    ref_obj* obj = s.MakeRef_obj();
    obj->VpiName(fullName);
    obj->VpiParent(path);
    elems->push_back(obj);
    fC->populateCoreMembers(variable, variable, obj);
    path->VpiFile(obj->VpiFile());
    while (fC->Type(Packed_dimension) == VObjectType::slStringConst) {
      ref_obj* obj = s.MakeRef_obj();
      const std::string_view name = fC->SymName(Packed_dimension);
      fullName.append(".").append(name);
      obj->VpiName(name);
      obj->VpiParent(path);
      fC->populateCoreMembers(Packed_dimension, Packed_dimension, obj);
      elems->push_back(obj);
      Packed_dimension = fC->Sibling(Packed_dimension);
    }
    path->VpiFullName(fullName);
    if (!elems->empty()) {
      path->VpiLineNo(elems->front()->VpiLineNo());
      path->VpiColumnNo(elems->front()->VpiColumnNo());
      path->VpiEndLineNo(elems->back()->VpiEndLineNo());
      path->VpiEndColumnNo(elems->back()->VpiEndColumnNo());
    }
    return path;
  }

  int32_t size;
  VectorOfrange* ranges =
      compileRanges(component, fC, Packed_dimension, compileDesign, reduce,
                    pstmt, instance, size, muteErrors);
  typespec* ts = nullptr;
  VObjectType decl_type = fC->Type(declarationId);
  if (decl_type != VObjectType::paPs_or_hierarchical_identifier &&
      decl_type != VObjectType::paImplicit_class_handle) {
    ts = compileTypespec(component, fC, declarationId, compileDesign, reduce,
                         pstmt, instance, true);
  }
  bool isSigned = true;
  const NodeId signId = fC->Sibling(variable);
  if (signId && (fC->Type(signId) == VObjectType::paSigning_Unsigned)) {
    isSigned = false;
  }
  switch (the_type) {
    case VObjectType::slStringConst:
    case VObjectType::paChandle_type: {
      const std::string_view typeName = fC->SymName(variable);

      if (const DataType* dt = component->getDataType(typeName)) {
        dt = dt->getActual();
        if (typespec* tps = dt->getTypespec()) {
          if (variables* var = getSimpleVarFromTypespec(
                  fC, declarationId, nameId, tps, ranges, compileDesign)) {
            var->VpiParent(pstmt);
            var->VpiName(fC->SymName(variable));
            if (ts) {
              if (var->Typespec() == nullptr) {
                ref_typespec* tsRef = s.MakeRef_typespec();
                tsRef->VpiName(typeName);
                tsRef->VpiParent(var);
                var->Typespec(tsRef);
                fC->populateCoreMembers(declarationId, declarationId, tsRef);
              }
              var->Typespec()->Actual_typespec(ts);
            }
            result = var;
          }
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
          class_var* var = s.MakeClass_var();
          if (ts == nullptr) {
            class_typespec* tps = s.MakeClass_typespec();
            tps->Class_defn(cl->getUhdmModel<UHDM::class_defn>());
            tps->VpiName(typespecName);
            tps->VpiParent(pstmt);
            ts = tps;
            fC->populateCoreMembers(variable, variable, tps);
          }
          ref_typespec* tpsRef = s.MakeRef_typespec();
          tpsRef->VpiName(typespecName);
          tpsRef->VpiParent(var);
          tpsRef->Actual_typespec(ts);
          var->Typespec(tpsRef);
          var->VpiName(fC->SymName(nameId));
          fC->populateCoreMembers(variable, variable, tpsRef);
          fC->populateCoreMembers(nameId, nameId, var);
          result = var;
        }
      }
      if (result == nullptr) {
        if (the_type == VObjectType::slStringConst) {
          if (ts) {
            if (ts->UhdmType() == uhdmclass_typespec) {
              class_var* var = s.MakeClass_var();
              ref_typespec* tsRef = s.MakeRef_typespec();
              tsRef->VpiParent(var);
              tsRef->Actual_typespec(ts);
              tsRef->VpiName(ts->VpiName());
              var->Typespec(tsRef);
              fC->populateCoreMembers(nameId, nameId, var);
              fC->populateCoreMembers(declarationId, declarationId, tsRef);
              result = var;
            }
          }
        }
      }
      if (result == nullptr) {
        if (the_type == VObjectType::paChandle_type) {
          chandle_var* var = s.MakeChandle_var();
          if (ts) {
            ref_typespec* tsRef = s.MakeRef_typespec();
            tsRef->VpiParent(var);
            tsRef->Actual_typespec(ts);
            tsRef->VpiName(ts->VpiName());
            fC->populateCoreMembers(nameId, nameId, var);
            fC->populateCoreMembers(declarationId, declarationId, tsRef);
            var->Typespec(tsRef);
          }
          result = var;
        } else {
          ref_var* ref = s.MakeRef_var();
          if (ts) {
            ref_typespec* tsRef = s.MakeRef_typespec();
            fC->populateCoreMembers(declarationId, declarationId, tsRef);
            tsRef->VpiParent(ref);
            tsRef->Actual_typespec(ts);
            tsRef->VpiName(typeName);
            ref->Typespec(tsRef);
          }
          ref->VpiName(typeName);
          fC->populateCoreMembers(nameId, nameId, ref);
          result = ref;
        }
      }
      break;
    }
    case VObjectType::paIntVec_TypeLogic:
    case VObjectType::paIntVec_TypeReg: {
      logic_var* var = s.MakeLogic_var();
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      var->Typespec(tsRef);
      var->VpiName(fC->SymName(nameId));
      fC->populateCoreMembers(nameId, nameId, var);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Int: {
      int_var* var = s.MakeInt_var();
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      var->Typespec(tsRef);
      var->VpiSigned(isSigned);
      var->VpiName(fC->SymName(nameId));
      fC->populateCoreMembers(nameId, nameId, var);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Integer: {
      integer_var* var = s.MakeInteger_var();
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      var->Typespec(tsRef);
      var->VpiSigned(isSigned);
      var->VpiName(fC->SymName(nameId));
      fC->populateCoreMembers(nameId, nameId, var);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      result = var;
      break;
    }
    case VObjectType::paSigning_Unsigned: {
      int_var* var = s.MakeInt_var();
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      var->Typespec(tsRef);
      var->VpiSigned(isSigned);
      var->VpiName(fC->SymName(nameId));
      fC->populateCoreMembers(nameId, nameId, var);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      result = var;
      break;
    }
    case VObjectType::paSigning_Signed: {
      int_var* var = s.MakeInt_var();
      if (ts != nullptr) {
        ref_typespec* tsRef = s.MakeRef_typespec();
        tsRef->VpiParent(var);
        tsRef->Actual_typespec(ts);
        var->Typespec(tsRef);
        fC->populateCoreMembers(declarationId, declarationId, tsRef);
      }
      var->VpiName(fC->SymName(nameId));
      fC->populateCoreMembers(nameId, nameId, var);
      var->VpiSigned(isSigned);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Byte: {
      byte_var* var = s.MakeByte_var();
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      var->Typespec(tsRef);
      var->VpiSigned(isSigned);
      var->VpiName(fC->SymName(nameId));
      fC->populateCoreMembers(nameId, nameId, var);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_LongInt: {
      long_int_var* var = s.MakeLong_int_var();
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      var->Typespec(tsRef);
      var->VpiSigned(isSigned);
      var->VpiName(fC->SymName(nameId));
      fC->populateCoreMembers(nameId, nameId, var);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Shortint: {
      short_int_var* var = s.MakeShort_int_var();
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      var->Typespec(tsRef);
      var->VpiSigned(isSigned);
      var->VpiName(fC->SymName(nameId));
      fC->populateCoreMembers(nameId, nameId, var);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Time: {
      time_var* var = s.MakeTime_var();
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      var->Typespec(tsRef);
      var->VpiName(fC->SymName(nameId));
      fC->populateCoreMembers(nameId, nameId, var);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      result = var;
      break;
    }
    case VObjectType::paIntVec_TypeBit: {
      bit_var* var = s.MakeBit_var();
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      var->Typespec(tsRef);
      var->VpiName(fC->SymName(nameId));
      fC->populateCoreMembers(nameId, nameId, var);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      result = var;
      break;
    }
    case VObjectType::paNonIntType_ShortReal: {
      short_real_var* var = s.MakeShort_real_var();
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      var->Typespec(tsRef);
      var->VpiName(fC->SymName(nameId));
      fC->populateCoreMembers(nameId, nameId, var);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      result = var;
      break;
    }
    case VObjectType::paNonIntType_Real: {
      real_var* var = s.MakeReal_var();
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      var->Typespec(tsRef);
      var->VpiName(fC->SymName(nameId));
      fC->populateCoreMembers(nameId, nameId, var);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      result = var;
      break;
    }
    case VObjectType::paClass_scope: {
      NodeId class_type = fC->Child(variable);
      NodeId class_name = fC->Child(class_type);
      const std::string_view packageName = fC->SymName(class_name);
      Design* design = compileDesign->getCompiler()->getDesign();
      NodeId symb_id = fC->Sibling(variable);
      const std::string_view typeName = fC->SymName(symb_id);
      Package* pack = design->getPackage(packageName);
      variables* var = nullptr;
      if (pack) {
        const DataType* dtype = pack->getDataType(typeName);
        while (dtype) {
          if (typespec* tps = dtype->getTypespec()) {
            var = getSimpleVarFromTypespec(fC, declarationId, nameId, tps,
                                           ranges, compileDesign);
            if (ts) {
              if (var->Typespec() == nullptr) {
                ref_typespec* tsRef = s.MakeRef_typespec();
                tsRef->VpiParent(var);
                var->Typespec(tsRef);
                fC->populateCoreMembers(declarationId, declarationId, tsRef);
              }
              var->Typespec()->Actual_typespec(ts);
            }
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
          const DataType* dtype = cl->getDataType(typeName);
          while (dtype) {
            if (typespec* tps = dtype->getTypespec()) {
              var = getSimpleVarFromTypespec(fC, declarationId, nameId, tps,
                                             ranges, compileDesign);
              if (ts) {
                if (var->Typespec() == nullptr) {
                  ref_typespec* tsRef = s.MakeRef_typespec();
                  tsRef->VpiParent(var);
                  var->Typespec(tsRef);
                  fC->populateCoreMembers(declarationId, declarationId, tsRef);
                }
                var->Typespec()->Actual_typespec(ts);
              }
              break;
            }
            dtype = dtype->getDefinition();
          }
        }
      }

      const std::string completeName = StrCat(packageName, "::", typeName);
      if (var == nullptr) var = s.MakeClass_var();
      var->VpiName(completeName);
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiName(completeName);
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      var->Typespec(tsRef);
      result = var;
      break;
    }
    case VObjectType::paString_type: {
      string_var* var = s.MakeString_var();
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      var->Typespec(tsRef);
      var->VpiName(fC->SymName(nameId));
      fC->populateCoreMembers(nameId, nameId, var);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      result = var;
      break;
    }
    case VObjectType::paVariable_lvalue: {
      NodeId hier_ident = fC->Child(variable);
      NodeId nameid = fC->Child(hier_ident);
      int_var* var = s.MakeInt_var();
      var->VpiName(fC->SymName(nameid));
      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(var);
      tsRef->Actual_typespec(ts);
      var->Typespec(tsRef);
      fC->populateCoreMembers(declarationId, declarationId, tsRef);
      result = var;
      break;
    }
    default: {
      // Implicit type
      if (declarationId) {
        logic_var* var = s.MakeLogic_var();
        var->VpiParent(pstmt);

        if (ts == nullptr) {
          logic_typespec* lts = s.MakeLogic_typespec();
          lts->VpiSigned(isSigned);
          lts->VpiParent(var);
          fC->populateCoreMembers(declarationId, declarationId, lts);
          if ((ranges != nullptr) && !ranges->empty()) {
            lts->Ranges(ranges);
            for (UHDM::range* r : *ranges) r->VpiParent(lts, true);
            lts->VpiEndLineNo(ranges->back()->VpiEndLineNo());
            lts->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
          }
          ts = lts;
        }

        ref_typespec* tsRef = s.MakeRef_typespec();
        tsRef->VpiParent(var);
        tsRef->Actual_typespec(ts);
        fC->populateCoreMembers(declarationId, declarationId, tsRef);
        var->Typespec(tsRef);

        result = var;
      }
      break;
    }
  }
  if (result != nullptr) {
    result->VpiParent(pstmt);
    fC->populateCoreMembers(nameId, nameId, result);
  }
  return result;
}

any* CompileHelper::compileVariable(
    DesignComponent* component, CompileDesign* compileDesign, Signal* sig,
    std::vector<UHDM::range*>* packedDimensions, int32_t packedSize,
    std::vector<UHDM::range*>* unpackedDimensions, int32_t unpackedSize,
    UHDM::expr* assignExp, UHDM::typespec* tps) {
  Serializer& s = compileDesign->getSerializer();
  const DataType* dtype = sig->getDataType();
  VObjectType subnettype = sig->getType();
  NodeId signalId = sig->getNameId();
  const std::string_view signame = sig->getName();
  const FileContent* const fC = sig->getFileContent();
  UHDM::any* pscope = component->getUhdmModel();
  if (pscope == nullptr)
    pscope = compileDesign->getCompiler()->getDesign()->getUhdmDesign();
  const NodeId rtBeginId = sig->getInterfaceTypeNameId()
                               ? sig->getInterfaceTypeNameId()
                               : sig->getTypespecId();
  const NodeId rtEndId =
      sig->getPackedDimension() ? sig->getPackedDimension() : rtBeginId;

  variables* obj = nullptr;
  bool found = false;
  while (dtype) {
    if (const TypeDef* tdef = datatype_cast<const TypeDef*>(dtype)) {
      if (tdef->getTypespec()) {
        tps = tdef->getTypespec();
        found = false;
        break;
      }
    } else if (const Enum* en = datatype_cast<const Enum*>(dtype)) {
      if (en->getTypespec()) {
        enum_var* stv = s.MakeEnum_var();
        if (typespec* ts = en->getTypespec()) {
          ref_typespec* tsRef = s.MakeRef_typespec();
          tsRef->VpiParent(stv);
          tsRef->Actual_typespec(ts);
          fC->populateCoreMembers(rtBeginId, rtEndId, tsRef);
          stv->Typespec(tsRef);
        }
        if (assignExp != nullptr) {
          stv->Expr(assignExp);
          assignExp->VpiParent(stv);
        }
        obj = stv;
        found = true;
        break;
      }
    } else if (const Struct* st = datatype_cast<const Struct*>(dtype)) {
      if (st->getTypespec()) {
        struct_var* stv = s.MakeStruct_var();
        if (typespec* ts = st->getTypespec()) {
          ref_typespec* tsRef = s.MakeRef_typespec();
          tsRef->VpiParent(stv);
          tsRef->Actual_typespec(ts);
          fC->populateCoreMembers(rtBeginId, rtEndId, tsRef);
          stv->Typespec(tsRef);
        }
        if (assignExp != nullptr) {
          stv->Expr(assignExp);
          assignExp->VpiParent(stv);
        }
        obj = stv;
        found = true;
        break;
      }
    } else if (const Union* un = datatype_cast<const Union*>(dtype)) {
      if (un->getTypespec()) {
        union_var* stv = s.MakeUnion_var();
        if (typespec* ts = un->getTypespec()) {
          ref_typespec* tsRef = s.MakeRef_typespec();
          tsRef->VpiParent(stv);
          tsRef->Actual_typespec(ts);
          fC->populateCoreMembers(rtBeginId, rtEndId, tsRef);
          stv->Typespec(tsRef);
        }
        if (assignExp != nullptr) {
          stv->Expr(assignExp);
          assignExp->VpiParent(stv);
        }
        obj = stv;
        found = true;
        break;
      }
    } else if (const DummyType* un = datatype_cast<const DummyType*>(dtype)) {
      typespec* tps = un->getTypespec();
      if (tps == nullptr) {
        tps =
            compileTypespec(component, un->getFileContent(), un->getNodeId(),
                            compileDesign, Reduce::Yes, nullptr, nullptr, true);
        ((DummyType*)un)->setTypespec(tps);
      }
      variables* var = nullptr;
      UHDM_OBJECT_TYPE ttps = tps->UhdmType();
      if (ttps == uhdmenum_typespec) {
        var = s.MakeEnum_var();
      } else if (ttps == uhdmstruct_typespec) {
        var = s.MakeStruct_var();
      } else if (ttps == uhdmunion_typespec) {
        var = s.MakeUnion_var();
      } else if (ttps == uhdmpacked_array_typespec) {
        var = s.MakePacked_array_var();
      } else if (ttps == uhdmarray_typespec) {
        UHDM::array_var* array_var = s.MakeArray_var();
        ref_typespec* tsRef = s.MakeRef_typespec();
        tsRef->VpiParent(array_var);
        tsRef->Actual_typespec(s.MakeArray_typespec());
        fC->populateCoreMembers(rtBeginId, rtEndId, tsRef);
        array_var->Typespec(tsRef);
        array_var->VpiArrayType(vpiStaticArray);
        array_var->VpiRandType(vpiNotRand);
        var = array_var;
      } else if (ttps == uhdmint_typespec) {
        var = s.MakeInt_var();
      } else if (ttps == uhdminteger_typespec) {
        var = s.MakeInteger_var();
      } else if (ttps == uhdmbyte_typespec) {
        var = s.MakeByte_var();
      } else if (ttps == uhdmbit_typespec) {
        var = s.MakeBit_var();
      } else if (ttps == uhdmshort_int_typespec) {
        var = s.MakeShort_int_var();
      } else if (ttps == uhdmlong_int_typespec) {
        var = s.MakeLong_int_var();
      } else if (ttps == uhdmstring_typespec) {
        var = s.MakeString_var();
      } else if (ttps == uhdmlogic_typespec) {
        logic_typespec* ltps = (logic_typespec*)tps;
        logic_var* avar = s.MakeLogic_var();
        if (auto ranges = ltps->Ranges()) {
          avar->Ranges(ranges);
          for (UHDM::range* r : *ranges) r->VpiParent(avar, true);
        }
        var = avar;
      } else {
        var = s.MakeLogic_var();
      }
      var->VpiName(signame);
      if (var->Typespec() == nullptr) {
        ref_typespec* tpsRef = s.MakeRef_typespec();
        tpsRef->VpiParent(var);
        fC->populateCoreMembers(rtBeginId, rtEndId, tpsRef);
        var->Typespec(tpsRef);
      }
      var->Typespec()->Actual_typespec(tps);
      if (assignExp != nullptr) {
        var->Expr(assignExp);
        assignExp->VpiParent(var);
      }
      obj = var;
      found = true;
      break;
    } else if (const SimpleType* sit =
                   datatype_cast<const SimpleType*>(dtype)) {
      UHDM::typespec* spec = sit->getTypespec();
      spec = elabTypespec(component, spec, compileDesign, nullptr, nullptr);
      variables* var =
          getSimpleVarFromTypespec(fC, sit->getNodeId(), sit->getNodeId(), spec,
                                   packedDimensions, compileDesign);
      var->VpiConstantVariable(sig->isConst());
      var->VpiSigned(sig->isSigned());
      var->VpiName(signame);
      if (var->Typespec() == nullptr) {
        ref_typespec* tsRef = s.MakeRef_typespec();
        tsRef->VpiParent(var);
        fC->populateCoreMembers(rtBeginId, rtEndId, tsRef);
        var->Typespec(tsRef);
      }
      var->Typespec()->Actual_typespec(spec);
      if (assignExp != nullptr) {
        var->Expr(assignExp);
        assignExp->VpiParent(var);
      }
      obj = var;
      found = true;
      break;
    } else if (/*const ClassDefinition* cl = */ datatype_cast<
               const ClassDefinition*>(dtype)) {
      class_var* stv = s.MakeClass_var();
      ref_typespec* tpsRef = s.MakeRef_typespec();
      tpsRef->VpiParent(stv);
      tpsRef->Actual_typespec(tps);
      fC->populateCoreMembers(rtBeginId, rtEndId, tpsRef);
      stv->Typespec(tpsRef);
      if (assignExp != nullptr) {
        stv->Expr(assignExp);
        assignExp->VpiParent(stv);
      }
      obj = stv;
      found = true;
      break;
    } else if (Parameter* sit = const_cast<Parameter*>(
                   datatype_cast<const Parameter*>(dtype))) {
      if (UHDM::typespec* spec =
              compileTypeParameter(component, compileDesign, sit)) {
        if (variables* var = getSimpleVarFromTypespec(
                fC, sit->getNodeId(), sit->getNodeId(), spec, packedDimensions,
                compileDesign)) {
          var->VpiConstantVariable(sig->isConst());
          var->VpiSigned(sig->isSigned());
          var->VpiName(signame);
          if (assignExp != nullptr) {
            var->Expr(assignExp);
            assignExp->VpiParent(var);
          }
          obj = var;
          found = true;
          break;
        }
      }
    }
    dtype = dtype->getDefinition();
  }

  if ((found == false) && tps) {
    UHDM::UHDM_OBJECT_TYPE tpstype = tps->UhdmType();
    if (tpstype == uhdmstruct_typespec) {
      struct_var* stv = s.MakeStruct_var();
      obj = stv;
    } else if (tpstype == uhdmlogic_typespec) {
      logic_var* stv = s.MakeLogic_var();
      // Do not set packedDimensions, it is a repeat of the typespec packed
      // dimension.
      // stv->Ranges(packedDimensions);
      obj = stv;
    } else if (tpstype == uhdmenum_typespec) {
      enum_var* stv = s.MakeEnum_var();
      obj = stv;
    } else if (tpstype == uhdmbit_typespec) {
      bit_var* stv = s.MakeBit_var();
      obj = stv;
    } else if (tpstype == uhdmbyte_typespec) {
      byte_var* stv = s.MakeByte_var();
      obj = stv;
    } else if (tpstype == uhdmreal_typespec) {
      real_var* stv = s.MakeReal_var();
      obj = stv;
    } else if (tpstype == uhdmint_typespec) {
      int_var* stv = s.MakeInt_var();
      obj = stv;
    } else if (tpstype == uhdminteger_typespec) {
      integer_var* stv = s.MakeInteger_var();
      obj = stv;
    } else if (tpstype == uhdmlong_int_typespec) {
      long_int_var* stv = s.MakeLong_int_var();
      obj = stv;
    } else if (tpstype == uhdmshort_int_typespec) {
      short_int_var* stv = s.MakeShort_int_var();
      obj = stv;
    } else if (tpstype == uhdmstring_typespec) {
      string_var* stv = s.MakeString_var();
      obj = stv;
    } else if (tpstype == uhdmbit_typespec) {
      bit_var* stv = s.MakeBit_var();
      obj = stv;
    } else if (tpstype == uhdmbyte_typespec) {
      byte_var* stv = s.MakeByte_var();
      obj = stv;
    } else if (tpstype == uhdmtime_typespec) {
      time_var* stv = s.MakeTime_var();
      obj = stv;
    } else if (tpstype == uhdmunion_typespec) {
      union_var* stv = s.MakeUnion_var();
      obj = stv;
    } else if (tpstype == uhdmclass_typespec) {
      class_var* stv = s.MakeClass_var();
      obj = stv;
    } else if (tpstype == uhdmpacked_array_typespec) {
      packed_array_var* stv = s.MakePacked_array_var();
      obj = stv;
    } else if (tpstype == uhdmarray_typespec) {
      UHDM::array_var* stv = s.MakeArray_var();
      obj = stv;
    }

    if (obj != nullptr) {
      obj->VpiName(signame);
      obj->VpiParent(pscope);
      if (assignExp != nullptr) {
        assignExp->VpiParent(obj);
        obj->Expr(assignExp);
      }
      if (tps != nullptr) {
        if (obj->Typespec() == nullptr) {
          ref_typespec* rt = s.MakeRef_typespec();
          rt->VpiParent(obj);
          obj->Typespec(rt);
          rt->VpiName(fC->SymName(rtBeginId));
          fC->populateCoreMembers(rtBeginId, rtEndId, rt);
          if ((tpstype == uhdmclass_typespec) &&
              (rt->VpiName().empty() ||
               (rt->VpiName() == SymbolTable::getBadSymbol())))
            rt->VpiName(tps->VpiName());
        }
        obj->Typespec()->Actual_typespec(tps);
        tps->VpiParent(obj);
      }
    }
  }

  if (obj == nullptr) {
    variables* var = nullptr;
    if (subnettype == VObjectType::paIntegerAtomType_Shortint) {
      UHDM::short_int_var* int_var = s.MakeShort_int_var();
      fC->populateCoreMembers(signalId, signalId, int_var);
      var = int_var;
      tps = s.MakeShort_int_typespec();
      tps->VpiParent(pscope);
      ref_typespec* tpsRef = s.MakeRef_typespec();
      tpsRef->VpiParent(int_var);
      tpsRef->Actual_typespec(tps);
      int_var->Typespec(tpsRef);
      fC->populateCoreMembers(rtBeginId, rtEndId, tpsRef);
      fC->populateCoreMembers(rtBeginId, rtEndId, tps);
    } else if (subnettype == VObjectType::paIntegerAtomType_Int) {
      UHDM::int_var* int_var = s.MakeInt_var();
      fC->populateCoreMembers(signalId, signalId, int_var);
      var = int_var;
      tps = s.MakeInt_typespec();
      tps->VpiParent(pscope);
      ref_typespec* tpsRef = s.MakeRef_typespec();
      tpsRef->VpiParent(int_var);
      tpsRef->Actual_typespec(tps);
      int_var->Typespec(tpsRef);
      fC->populateCoreMembers(rtBeginId, rtEndId, tpsRef);
      fC->populateCoreMembers(rtBeginId, rtEndId, tps);
    } else if (subnettype == VObjectType::paIntegerAtomType_Integer) {
      UHDM::integer_var* int_var = s.MakeInteger_var();
      fC->populateCoreMembers(signalId, signalId, int_var);
      var = int_var;
      tps = s.MakeInteger_typespec();
      tps->VpiParent(pscope);
      ref_typespec* tpsRef = s.MakeRef_typespec();
      tpsRef->VpiParent(int_var);
      tpsRef->Actual_typespec(tps);
      int_var->Typespec(tpsRef);
      fC->populateCoreMembers(rtBeginId, rtEndId, tpsRef);
      fC->populateCoreMembers(rtBeginId, rtEndId, tps);
    } else if (subnettype == VObjectType::paIntegerAtomType_LongInt) {
      UHDM::long_int_var* int_var = s.MakeLong_int_var();
      fC->populateCoreMembers(signalId, signalId, int_var);
      var = int_var;
      tps = s.MakeLong_int_typespec();
      tps->VpiParent(pscope);
      ref_typespec* tpsRef = s.MakeRef_typespec();
      tpsRef->VpiParent(int_var);
      tpsRef->Actual_typespec(tps);
      int_var->Typespec(tpsRef);
      fC->populateCoreMembers(rtBeginId, rtEndId, tpsRef);
      fC->populateCoreMembers(rtBeginId, rtEndId, tps);
    } else if (subnettype == VObjectType::paIntegerAtomType_Time) {
      UHDM::time_var* int_var = s.MakeTime_var();
      fC->populateCoreMembers(signalId, signalId, int_var);
      var = int_var;
    } else if (subnettype == VObjectType::paIntVec_TypeBit) {
      UHDM::bit_var* int_var = s.MakeBit_var();
      fC->populateCoreMembers(signalId, signalId, int_var);
      bit_typespec* btps = s.MakeBit_typespec();
      if (packedDimensions != nullptr) {
        btps->Ranges(packedDimensions);
        for (UHDM::range* r : *packedDimensions) r->VpiParent(btps, true);
      }
      btps->VpiParent(pscope);
      tps = btps;
      ref_typespec* tpsRef = s.MakeRef_typespec();
      tpsRef->VpiParent(int_var);
      tpsRef->Actual_typespec(tps);
      int_var->Typespec(tpsRef);
      fC->populateCoreMembers(rtBeginId, rtEndId, tpsRef);
      fC->populateCoreMembers(rtBeginId, rtEndId, tps);
      var = int_var;
    } else if (subnettype == VObjectType::paIntegerAtomType_Byte) {
      UHDM::byte_var* int_var = s.MakeByte_var();
      fC->populateCoreMembers(signalId, signalId, int_var);
      byte_typespec* btps = s.MakeByte_typespec();
      btps->VpiParent(pscope);
      tps = btps;
      ref_typespec* tpsRef = s.MakeRef_typespec();
      tpsRef->VpiParent(int_var);
      tpsRef->Actual_typespec(tps);
      int_var->Typespec(tpsRef);
      fC->populateCoreMembers(rtBeginId, rtEndId, tpsRef);
      fC->populateCoreMembers(rtBeginId, rtEndId, tps);
      var = int_var;
    } else if (subnettype == VObjectType::paNonIntType_ShortReal) {
      UHDM::short_real_var* int_var = s.MakeShort_real_var();
      fC->populateCoreMembers(signalId, signalId, int_var);
      var = int_var;
    } else if (subnettype == VObjectType::paNonIntType_Real) {
      UHDM::real_var* int_var = s.MakeReal_var();
      fC->populateCoreMembers(signalId, signalId, int_var);
      var = int_var;
    } else if (subnettype == VObjectType::paNonIntType_RealTime) {
      UHDM::time_var* int_var = s.MakeTime_var();
      fC->populateCoreMembers(signalId, signalId, int_var);
      var = int_var;
    } else if (subnettype == VObjectType::paString_type) {
      UHDM::string_var* int_var = s.MakeString_var();
      fC->populateCoreMembers(signalId, signalId, int_var);
      var = int_var;
    } else if (subnettype == VObjectType::paChandle_type) {
      UHDM::chandle_var* chandle_var = s.MakeChandle_var();
      fC->populateCoreMembers(signalId, signalId, chandle_var);
      var = chandle_var;
    } else if (subnettype == VObjectType::paIntVec_TypeLogic) {
      logic_var* logicv = s.MakeLogic_var();
      logic_typespec* ltps = s.MakeLogic_typespec();
      ltps->VpiParent(pscope);
      NodeId id;
      if (sig->getPackedDimension()) id = fC->Parent(sig->getPackedDimension());
      if (!id) id = sig->getNodeId();
      if (id) fC->populateCoreMembers(id, id, ltps);
      if ((packedDimensions != nullptr) && !packedDimensions->empty()) {
        ltps->Ranges(packedDimensions);
        for (UHDM::range* r : *packedDimensions) r->VpiParent(ltps, true);
        ltps->VpiEndLineNo(packedDimensions->back()->VpiEndLineNo());
        ltps->VpiEndColumnNo(packedDimensions->back()->VpiEndColumnNo());
      }
      tps = ltps;
      ref_typespec* tpsRef = s.MakeRef_typespec();
      tpsRef->VpiParent(logicv);
      tpsRef->Actual_typespec(tps);
      fC->populateCoreMembers(rtBeginId, rtEndId, tpsRef);
      logicv->Typespec(tpsRef);
      var = logicv;
    } else if (subnettype == VObjectType::paEvent_type) {
      named_event* event = s.MakeNamed_event();
      event->VpiName(signame);
      return event;
    } else {
      // default type (fallback)
      logic_var* logicv = s.MakeLogic_var();
      if (packedDimensions != nullptr) {
        logicv->Ranges(packedDimensions);
        for (UHDM::range* r : *packedDimensions) r->VpiParent(logicv, true);
      }
      var = logicv;
    }
    var->VpiSigned(sig->isSigned());
    var->VpiConstantVariable(sig->isConst());
    var->VpiName(signame);
    if (assignExp != nullptr) {
      var->Expr(assignExp);
      assignExp->VpiParent(var);
    }
    obj = var;
  } else if (packedDimensions && (obj->UhdmType() != uhdmlogic_var) &&
             (obj->UhdmType() != uhdmbit_var) &&
             (obj->UhdmType() != uhdmpacked_array_var)) {
    // packed struct array ...
    UHDM::packed_array_var* parray = s.MakePacked_array_var();
    if (packedDimensions != nullptr) {
      parray->Ranges(packedDimensions);
      for (UHDM::range* r : *packedDimensions) r->VpiParent(parray, true);
    }
    parray->Elements(true)->push_back(obj);
    obj->VpiParent(parray);
    parray->VpiName(signame);
    obj = parray;
  }

  if (unpackedDimensions) {
    UHDM::array_var* array_var = s.MakeArray_var();
    array_var->VpiParent(pscope);
    bool dynamic = false;
    bool associative = false;
    bool queue = false;
    int32_t index = 0;
    for (auto itr = unpackedDimensions->begin();
         itr != unpackedDimensions->end(); itr++) {
      range* r = *itr;
      const expr* rhs = r->Right_expr();
      if (rhs->UhdmType() == uhdmconstant) {
        const std::string_view value = rhs->VpiValue();
        if (value == "STRING:$") {
          queue = true;
          unpackedDimensions->erase(itr);
          break;
        } else if (value == "STRING:associative") {
          associative = true;
          const typespec* tp = nullptr;
          if (const ref_typespec* rt = rhs->Typespec()) {
            tp = rt->Actual_typespec();
          }

          array_typespec* taps = s.MakeArray_typespec();
          taps->VpiParent(pscope);
          fC->populateCoreMembers(sig->getUnpackedDimension(),
                                  sig->getUnpackedDimension(), taps);

          if (tp != nullptr) {
            ref_typespec* tpRef = s.MakeRef_typespec();
            tpRef->VpiParent(taps);
            tpRef->VpiName(tp->VpiName());
            tpRef->Actual_typespec(const_cast<UHDM::typespec*>(tp));
            taps->Index_typespec(tpRef);
            fC->populateCoreMembers(sig->getUnpackedDimension(),
                                    sig->getUnpackedDimension(), tpRef);
          }

          ref_typespec* taps_ref = s.MakeRef_typespec();
          taps_ref->VpiParent(array_var);
          taps_ref->Actual_typespec(taps);
          taps_ref->VpiName(array_var->VpiName());
          fC->populateCoreMembers(sig->getUnpackedDimension(),
                                  sig->getUnpackedDimension(), taps_ref);
          array_var->Typespec(taps_ref);
          unpackedDimensions->erase(itr);
          break;
        } else if (value == "STRING:unsized") {
          dynamic = true;
          unpackedDimensions->erase(itr);
          break;
        }
      }
      index++;
    }

    if (associative || queue || dynamic) {
      if (!unpackedDimensions->empty()) {
        if (index == 0) {
          array_var->Ranges(unpackedDimensions);
          for (UHDM::range* r : *unpackedDimensions)
            r->VpiParent(array_var, true);
        } else {
          array_typespec* tps = s.MakeArray_typespec();
          ref_typespec* tpsRef = s.MakeRef_typespec();
          tpsRef->VpiParent(array_var);
          tps->VpiParent(pscope);
          tpsRef->Actual_typespec(tps);
          NodeId unpackDimensionId = sig->getUnpackedDimension();
          while (fC->Sibling(unpackDimensionId))
            unpackDimensionId = fC->Sibling(unpackDimensionId);
          fC->populateCoreMembers(unpackDimensionId, unpackDimensionId, tpsRef);
          fC->populateCoreMembers(unpackDimensionId, unpackDimensionId, tps);
          array_var->Typespec(tpsRef);

          if (associative)
            tps->VpiArrayType(vpiAssocArray);
          else if (queue)
            tps->VpiArrayType(vpiQueueArray);
          else if (dynamic)
            tps->VpiArrayType(vpiDynamicArray);
          else
            tps->VpiArrayType(vpiStaticArray);

          array_typespec* subtps = s.MakeArray_typespec();
          subtps->VpiParent(pscope);
          fC->populateCoreMembers(sig->getUnpackedDimension(),
                                  sig->getUnpackedDimension(), subtps);
          array_var->Typespec(tpsRef);
          tpsRef = s.MakeRef_typespec();
          fC->populateCoreMembers(signalId, signalId, tpsRef);
          tpsRef->VpiParent(tps);
          tpsRef->Actual_typespec(subtps);
          tps->Elem_typespec(tpsRef);

          subtps->Ranges(unpackedDimensions);
          for (UHDM::range* r : *unpackedDimensions) r->VpiParent(subtps, true);
          subtps->VpiEndLineNo(unpackedDimensions->back()->VpiEndLineNo());
          subtps->VpiEndColumnNo(unpackedDimensions->back()->VpiEndColumnNo());

          switch (obj->UhdmType()) {
            case uhdmint_var: {
              int_typespec* ts = s.MakeInt_typespec();
              fC->populateCoreMembers(rtBeginId, rtEndId, ts);
              ts->VpiParent(pscope);
              tpsRef = s.MakeRef_typespec();
              tpsRef->VpiParent(subtps);
              tpsRef->Actual_typespec(ts);
              subtps->Elem_typespec(tpsRef);
              break;
            }
            case uhdminteger_var: {
              integer_typespec* ts = s.MakeInteger_typespec();
              fC->populateCoreMembers(rtBeginId, rtEndId, ts);
              ts->VpiParent(pscope);
              tpsRef = s.MakeRef_typespec();
              tpsRef->VpiParent(subtps);
              tpsRef->Actual_typespec(ts);
              subtps->Elem_typespec(tpsRef);
              break;
            }
            case uhdmlogic_var: {
              logic_typespec* ts = s.MakeLogic_typespec();
              fC->populateCoreMembers(rtBeginId, rtEndId, ts);
              ts->VpiParent(pscope);
              tpsRef = s.MakeRef_typespec();
              tpsRef->VpiParent(subtps);
              tpsRef->Actual_typespec(ts);
              subtps->Elem_typespec(tpsRef);
              break;
            }
            case uhdmlong_int_var: {
              long_int_typespec* ts = s.MakeLong_int_typespec();
              fC->populateCoreMembers(rtBeginId, rtEndId, ts);
              ts->VpiParent(pscope);
              tpsRef = s.MakeRef_typespec();
              tpsRef->VpiParent(subtps);
              tpsRef->Actual_typespec(ts);
              subtps->Elem_typespec(tpsRef);
              break;
            }
            case uhdmshort_int_var: {
              short_int_typespec* ts = s.MakeShort_int_typespec();
              fC->populateCoreMembers(rtBeginId, rtEndId, ts);
              ts->VpiParent(pscope);
              tpsRef = s.MakeRef_typespec();
              tpsRef->VpiParent(subtps);
              tpsRef->Actual_typespec(ts);
              subtps->Elem_typespec(tpsRef);
              break;
            }
            case uhdmbyte_var: {
              byte_typespec* ts = s.MakeByte_typespec();
              fC->populateCoreMembers(rtBeginId, rtEndId, ts);
              ts->VpiParent(pscope);
              tpsRef = s.MakeRef_typespec();
              tpsRef->VpiParent(subtps);
              tpsRef->Actual_typespec(ts);
              subtps->Elem_typespec(tpsRef);
              break;
            }
            case uhdmbit_var: {
              bit_typespec* ts = s.MakeBit_typespec();
              fC->populateCoreMembers(rtBeginId, rtEndId, ts);
              ts->VpiParent(pscope);
              tpsRef = s.MakeRef_typespec();
              tpsRef->VpiParent(subtps);
              tpsRef->Actual_typespec(ts);
              subtps->Elem_typespec(tpsRef);
              break;
            }
            case uhdmstring_var: {
              string_typespec* ts = s.MakeString_typespec();
              fC->populateCoreMembers(rtBeginId, rtEndId, ts);
              ts->VpiParent(pscope);
              tpsRef = s.MakeRef_typespec();
              tpsRef->VpiParent(subtps);
              tpsRef->Actual_typespec(ts);
              subtps->Elem_typespec(tpsRef);
              break;
            }
            default: {
              unsupported_typespec* ts = s.MakeUnsupported_typespec();
              tpsRef = s.MakeRef_typespec();
              ts->VpiName(fC->SymName(rtBeginId));
              ts->VpiParent(pscope);
              fC->populateCoreMembers(rtBeginId, rtEndId, ts);
              tpsRef->VpiName(fC->SymName(rtBeginId));
              tpsRef->VpiParent(subtps);
              tpsRef->Actual_typespec(ts);
              subtps->Elem_typespec(tpsRef);
              break;
            }
          }
          fC->populateCoreMembers(rtBeginId, rtEndId, tpsRef);
        }
      }
    }

    if (associative) {
      array_var->VpiArrayType(vpiAssocArray);
    } else if (queue) {
      array_var->VpiArrayType(vpiQueueArray);
    } else if (dynamic) {
      array_var->VpiArrayType(vpiDynamicArray);
    } else {
      if (unpackedDimensions != nullptr) {
        array_var->Ranges(unpackedDimensions);
        for (UHDM::range* r : *unpackedDimensions)
          r->VpiParent(array_var, true);
      }
      array_var->VpiArrayType(vpiStaticArray);
    }
    array_var->VpiSize(unpackedSize);
    array_var->VpiName(signame);
    array_var->VpiRandType(vpiNotRand);
    array_var->VpiVisibility(vpiPublicVis);
    fC->populateCoreMembers(sig->getNameId(), sig->getNameId(), array_var);

    obj->VpiParent(pscope);
    if ((array_var->Typespec() == nullptr) || associative) {
      array_var->Variables(true)->push_back((variables*)obj);
      ((variables*)obj)->VpiName("");
    }
    if (array_var->Typespec() == nullptr) {
      array_typespec* attps = s.MakeArray_typespec();
      fC->populateCoreMembers(sig->getUnpackedDimension(),
                              sig->getUnpackedDimension(), attps);
      attps->VpiParent(pscope);

      ref_typespec* tsRef = s.MakeRef_typespec();
      tsRef->VpiParent(array_var);
      tsRef->Actual_typespec(attps);
      array_var->Typespec(tsRef);
      fC->populateCoreMembers(sig->getUnpackedDimension(),
                              sig->getUnpackedDimension(), tsRef);
    }
    if (assignExp != nullptr) {
      array_var->Expr(assignExp);
      assignExp->VpiParent(array_var);
    }
    fC->populateCoreMembers(sig->getNameId(), sig->getNameId(), obj);
    obj = array_var;
  } else {
    if (obj->UhdmType() == uhdmenum_var) {
      ((enum_var*)obj)->VpiName(signame);
    } else if (obj->UhdmType() == uhdmstruct_var) {
      ((struct_var*)obj)->VpiName(signame);
    } else if (obj->UhdmType() == uhdmunion_var) {
      ((union_var*)obj)->VpiName(signame);
    } else if (obj->UhdmType() == uhdmclass_var) {
      ((class_var*)obj)->VpiName(signame);
    } else if (obj->UhdmType() == uhdmlogic_var) {
      ((logic_var*)obj)->VpiName(signame);
    }
  }

  if (assignExp) {
    if (assignExp->UhdmType() == uhdmconstant) {
      adjustSize(tps, component, compileDesign, nullptr, (constant*)assignExp);
    } else if (assignExp->UhdmType() == uhdmoperation) {
      operation* op = (operation*)assignExp;
      int32_t opType = op->VpiOpType();
      const typespec* tp = tps;
      if (opType == vpiAssignmentPatternOp) {
        if (tp->UhdmType() == uhdmpacked_array_typespec) {
          packed_array_typespec* ptp = (packed_array_typespec*)tp;
          if (const ref_typespec* ert = ptp->Elem_typespec()) {
            tp = ert->Actual_typespec();
          }
          if (tp == nullptr) tp = tps;
        }
      }
      for (auto oper : *op->Operands()) {
        if (oper->UhdmType() == uhdmconstant)
          adjustSize(tp, component, compileDesign, nullptr, (constant*)oper,
                     false, true);
      }
    }
  }

  if (obj) {
    if (packedDimensions != nullptr) {
      for (auto r : *packedDimensions) r->VpiParent(obj);
    }
    if (unpackedDimensions != nullptr) {
      for (auto r : *unpackedDimensions) r->VpiParent(obj);
    }

    if (assignExp != nullptr) {
      obj->Expr(assignExp);
      assignExp->VpiParent(obj);
    }
    obj->VpiSigned(sig->isSigned());
    obj->VpiConstantVariable(sig->isConst());
    obj->VpiIsRandomized(sig->isRand() || sig->isRandc());
    if (sig->isRand())
      obj->VpiRandType(vpiRand);
    else if (sig->isRandc())
      obj->VpiRandType(vpiRandC);
    if (sig->isStatic()) {
      obj->VpiAutomatic(false);
    } else {
      obj->VpiAutomatic(true);
    }
    if (sig->isProtected()) {
      obj->VpiVisibility(vpiProtectedVis);
    } else if (sig->isLocal()) {
      obj->VpiVisibility(vpiLocalVis);
    } else {
      obj->VpiVisibility(vpiPublicVis);
    }
  }
  obj->VpiParent(pscope);
  return obj;
}

UHDM::typespec* CompileHelper::compileTypeParameter(
    DesignComponent* component, CompileDesign* compileDesign, Parameter* sit) {
  Serializer& s = compileDesign->getSerializer();
  UHDM::typespec* spec = nullptr;
  bool type_param = false;
  if (UHDM::any* uparam = sit->getUhdmParam()) {
    if (uparam->UhdmType() == uhdmtype_parameter) {
      if (ref_typespec* rt = ((type_parameter*)uparam)->Typespec())
        spec = rt->Actual_typespec();
      type_param = true;
    } else {
      if (ref_typespec* rt = ((parameter*)uparam)->Typespec())
        spec = rt->Actual_typespec();
    }
  }

  const std::string_view pname = sit->getName();
  Parameter* param = component->getParameter(pname);

  UHDM::any* uparam = param->getUhdmParam();
  UHDM::typespec* override_spec = nullptr;
  if (uparam == nullptr) {
    if (type_param) {
      type_parameter* tp = s.MakeType_parameter();
      tp->VpiName(pname);
      param->setUhdmParam(tp);
    } else {
      parameter* tp = s.MakeParameter();
      tp->VpiName(pname);
      param->setUhdmParam(tp);
    }
    uparam = param->getUhdmParam();
  }

  if (type_param) {
    if (ref_typespec* rt = ((type_parameter*)uparam)->Typespec()) {
      override_spec = rt->Actual_typespec();
    }
  } else {
    if (ref_typespec* rt = ((parameter*)uparam)->Typespec()) {
      override_spec = rt->Actual_typespec();
    }
  }

  if (override_spec == nullptr) {
    override_spec = compileTypespec(component, param->getFileContent(),
                                    param->getNodeType(), compileDesign,
                                    Reduce::Yes, nullptr, nullptr, false);
  }

  if (override_spec) {
    if (type_param) {
      type_parameter* tparam = (type_parameter*)uparam;
      if (tparam->Typespec() == nullptr) {
        ref_typespec* override_specRef = s.MakeRef_typespec();
        override_specRef->VpiParent(tparam);
        tparam->Typespec(override_specRef);
      }
      tparam->Typespec()->Actual_typespec(override_spec);
    } else {
      parameter* tparam = (parameter*)uparam;
      if (tparam->Typespec() == nullptr) {
        ref_typespec* override_specRef = s.MakeRef_typespec();
        override_specRef->VpiParent(tparam);
        tparam->Typespec(override_specRef);
      }
      tparam->Typespec()->Actual_typespec(override_spec);
    }
    spec = override_spec;
    spec->VpiParent(uparam);
  }
  return spec;
}

const UHDM::typespec* bindTypespec(std::string_view name,
                                   SURELOG::ValuedComponentI* instance,
                                   Serializer& s) {
  const typespec* result = nullptr;
  ModuleInstance* modInst = valuedcomponenti_cast<ModuleInstance*>(instance);
  while (modInst) {
    for (Parameter* param : modInst->getTypeParams()) {
      const std::string_view pname = param->getName();
      if (pname == name) {
        if (any* uparam = param->getUhdmParam()) {
          if (type_parameter* tparam = any_cast<type_parameter*>(uparam)) {
            if (const ref_typespec* rt = tparam->Typespec()) {
              result = rt->Actual_typespec();
            }
            ElaboratorContext elaboratorContext(&s, false, true);
            result = any_cast<typespec*>(
                UHDM::clone_tree((any*)result, &elaboratorContext));
          }
        }
        break;
      }
    }
    if (result == nullptr) {
      if (ModuleDefinition* mod = (ModuleDefinition*)modInst->getDefinition()) {
        if (Parameter* param = mod->getParameter(name)) {
          if (any* uparam = param->getUhdmParam()) {
            if (type_parameter* tparam = any_cast<type_parameter*>(uparam)) {
              if (const ref_typespec* rt = tparam->Typespec()) {
                result = rt->Actual_typespec();
              }
              ElaboratorContext elaboratorContext(&s, false, true);
              result = any_cast<typespec*>(
                  UHDM::clone_tree((any*)result, &elaboratorContext));
            }
          }
        }
        if (const DataType* dt = mod->getDataType(name)) {
          dt = dt->getActual();
          result = dt->getTypespec();
          ElaboratorContext elaboratorContext(&s, false, true);
          result = any_cast<typespec*>(
              UHDM::clone_tree((any*)result, &elaboratorContext));
        }
      }
    }
    modInst = modInst->getParent();
  }
  return result;
}

typespec* CompileHelper::compileDatastructureTypespec(
    DesignComponent* component, const FileContent* fC, NodeId type,
    CompileDesign* compileDesign, Reduce reduce,
    SURELOG::ValuedComponentI* instance, std::string_view suffixname,
    std::string_view typeName) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();
  CommandLineParser* const clp = m_session->getCommandLineParser();
  UHDM::any* pscope = component->getUhdmModel();
  if (pscope == nullptr)
    pscope = compileDesign->getCompiler()->getDesign()->getUhdmDesign();
  UHDM::Serializer& s = compileDesign->getSerializer();
  typespec* result = nullptr;
  if (component) {
    const DataType* dt = component->getDataType(typeName);
    if (dt == nullptr) {
      const std::string_view libName = fC->getLibrary()->getName();
      dt = compileDesign->getCompiler()->getDesign()->getClassDefinition(
          StrCat(libName, "@", typeName));
      if (dt == nullptr) {
        dt = compileDesign->getCompiler()->getDesign()->getClassDefinition(
            StrCat(component->getName(), "::", typeName));
      }
      if (dt == nullptr) {
        if (component->getParentScope())
          dt = compileDesign->getCompiler()->getDesign()->getClassDefinition(
              StrCat(((DesignComponent*)component->getParentScope())->getName(),
                     "::", typeName));
      }
      if (dt == nullptr) {
        dt = compileDesign->getCompiler()->getDesign()->getClassDefinition(
            typeName);
      }
      if (dt == nullptr) {
        Parameter* p = component->getParameter(typeName);
        if (p && p->getUhdmParam() &&
            (p->getUhdmParam()->UhdmType() == uhdmtype_parameter))
          dt = p;
      }
      if (dt == nullptr) {
        for (ParamAssign* passign : component->getParamAssignVec()) {
          const FileContent* fCP = passign->getFileContent();
          if (fCP->SymName(passign->getParamId()) == typeName) {
            UHDM::param_assign* param_assign = passign->getUhdmParamAssign();
            UHDM::parameter* lhs = (UHDM::parameter*)param_assign->Lhs();
            if (ref_typespec* rt = lhs->Typespec()) {
              result = rt->Actual_typespec();
            }
            if (result == nullptr) {
              if (int_typespec* tps = buildIntTypespec(
                      compileDesign, fC->getFileId(), typeName, "",
                      fC->Line(type), fC->Column(type), fC->EndLine(type),
                      fC->EndColumn(type))) {
                result = tps;
              }
            }
            if (result->UhdmType() == uhdmint_typespec) {
              int_typespec* ts = (int_typespec*)result;
              ref_obj* ref = s.MakeRef_obj();
              ref->Actual_group(lhs);
              ref->VpiName(typeName);
              ref->VpiParent(ts);
              ts->Cast_to_expr(ref);
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
                VObjectType::slStringConst) {
              typeName2 = fC->SymName(sig->getInterfaceTypeNameId());
            }
            NodeId suffixNode;
            if ((suffixNode = fC->Sibling(type))) {
              if (fC->Type(suffixNode) == VObjectType::slStringConst) {
                suffixname = fC->SymName(suffixNode);
              } else if (fC->Type(suffixNode) ==
                         VObjectType::paConstant_bit_select) {
                suffixNode = fC->Sibling(suffixNode);
                if (fC->Type(suffixNode) == VObjectType::slStringConst) {
                  suffixname = fC->SymName(suffixNode);
                }
              }
            }
            typespec* tmp = compileDatastructureTypespec(
                component, fC, sig->getInterfaceTypeNameId(), compileDesign,
                reduce, instance, suffixname, typeName2);
            if (tmp) {
              if (tmp->UhdmType() == uhdminterface_typespec) {
                if (!suffixname.empty()) {
                  Location loc1(fC->getFileId(), fC->Line(suffixNode),
                                fC->Column(suffixNode),
                                symbols->registerSymbol(suffixname));
                  const std::string_view libName = fC->getLibrary()->getName();
                  Design* design = compileDesign->getCompiler()->getDesign();
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
        for (const auto& fC :
             compileDesign->getCompiler()->getDesign()->getAllFileContents()) {
          if (const DataType* dt1 = fC.second->getDataType(typeName)) {
            dt = dt1;
            break;
          }
        }
      }
    }

    TypeDef* parent_tpd = nullptr;
    while (dt) {
      if (const TypeDef* tpd = datatype_cast<const TypeDef*>(dt)) {
        parent_tpd = (TypeDef*)tpd;
        if (parent_tpd->getTypespec()) {
          result = parent_tpd->getTypespec();
          break;
        }
      } else if (const Struct* st = datatype_cast<const Struct*>(dt)) {
        result = st->getTypespec();
        if (!suffixname.empty()) {
          struct_typespec* tpss = (struct_typespec*)result;
          for (typespec_member* memb : *tpss->Members()) {
            if (memb->VpiName() == suffixname) {
              if (ref_typespec* rt = memb->Typespec()) {
                result = rt->Actual_typespec();
              }
              break;
            }
          }
        }
        break;
      } else if (const Enum* en = datatype_cast<const Enum*>(dt)) {
        result = en->getTypespec();
        break;
      } else if (const Union* un = datatype_cast<const Union*>(dt)) {
        result = un->getTypespec();
        break;
      } else if (const DummyType* un = datatype_cast<const DummyType*>(dt)) {
        result = un->getTypespec();
      } else if (const SimpleType* sit = datatype_cast<const SimpleType*>(dt)) {
        result = sit->getTypespec();
        if ((m_elaborate == Elaborate::Yes) && parent_tpd && result) {
          ElaboratorContext elaboratorContext(&s, false, true);
          if (typespec* new_result = any_cast<typespec*>(
                  UHDM::clone_tree((any*)result, &elaboratorContext))) {
            if (typedef_typespec* const tt =
                    any_cast<UHDM::typedef_typespec>(new_result)) {
              if (tt->Typedef_alias() == nullptr) {
                ref_typespec* tsRef = s.MakeRef_typespec();
                fC->populateCoreMembers(type, type, tsRef);
                tsRef->VpiParent(new_result);
                tt->Typedef_alias(tsRef);
              }
              tt->Typedef_alias()->Actual_typespec(result);
            }
            result = new_result;
          }
        }
        break;
      } else if (/*const Parameter* par = */ datatype_cast<const Parameter*>(
          dt)) {
        // Prevent circular definition
        return nullptr;
      } else if (const ClassDefinition* classDefn =
                     datatype_cast<const ClassDefinition*>(dt)) {
        class_typespec* ref = s.MakeClass_typespec();
        ref->Class_defn(classDefn->getUhdmModel<UHDM::class_defn>());
        ref->VpiName(typeName);
        ref->VpiParent(pscope);
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
                      VObjectType::paList_of_net_decl_assignments)) {
          VectorOfany* params = ref->Parameters(true);
          VectorOfparam_assign* assigns = ref->Param_assigns(true);
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
              any* fparam = nullptr;
              if (index < formal.size()) {
                Parameter* p = formal.at(index);
                fName = p->getName();
                fparam = p->getUhdmParam();

                if (actualFC->Type(Data_type) == VObjectType::paData_type) {
                  typespec* tps = compileTypespec(
                      component, actualFC, Data_type, compileDesign, reduce,
                      result, instance, false);

                  type_parameter* tp = s.MakeType_parameter();
                  tp->VpiName(fName);
                  tp->VpiParent(ref);
                  tps->VpiParent(tp);
                  ref_typespec* tpsRef = s.MakeRef_typespec();
                  tpsRef->VpiParent(tp);
                  tpsRef->VpiName(fName);
                  tpsRef->Actual_typespec(tps);
                  tp->Typespec(tpsRef);
                  p->getFileContent()->populateCoreMembers(p->getNodeId(),
                                                           p->getNodeId(), tp);
                  p->getFileContent()->populateCoreMembers(
                      p->getNodeId(), p->getNodeId(), tpsRef);
                  params->push_back(tp);
                  param_assign* pass = s.MakeParam_assign();
                  pass->Rhs(tp);
                  pass->Lhs(fparam);
                  pass->VpiParent(ref);
                  pass->VpiLineNo(fparam->VpiLineNo());
                  pass->VpiColumnNo(fparam->VpiColumnNo());
                  pass->VpiEndLineNo(tp->VpiEndLineNo());
                  pass->VpiEndColumnNo(tp->VpiEndColumnNo());
                  assigns->push_back(pass);
                } else if (any* exp = compileExpression(
                               component, actualFC, Param_expression,
                               compileDesign, reduce, nullptr, instance)) {
                  if (exp->UhdmType() == uhdmref_obj) {
                    const std::string_view name = ((ref_obj*)exp)->VpiName();
                    if (typespec* tps = compileDatastructureTypespec(
                            component, actualFC, param, compileDesign, reduce,
                            instance, "", name)) {
                      type_parameter* tp = s.MakeType_parameter();
                      tp->VpiName(fName);
                      ref_typespec* tpsRef = s.MakeRef_typespec();
                      tpsRef->VpiParent(tp);
                      tpsRef->VpiName(name);
                      tpsRef->Actual_typespec(tps);
                      p->getFileContent()->populateCoreMembers(
                          p->getNodeId(), p->getNodeId(), tp);
                      p->getFileContent()->populateCoreMembers(
                          p->getNodeId(), p->getNodeId(), tpsRef);
                      tp->Typespec(tpsRef);
                      tps->VpiParent(tp);
                      tp->VpiParent(ref);
                      params->push_back(tp);
                      param_assign* pass = s.MakeParam_assign();
                      pass->Rhs(tp);
                      pass->Lhs(fparam);
                      pass->VpiParent(ref);
                      pass->VpiLineNo(fparam->VpiLineNo());
                      pass->VpiColumnNo(fparam->VpiColumnNo());
                      pass->VpiEndLineNo(tp->VpiEndLineNo());
                      pass->VpiEndColumnNo(tp->VpiEndColumnNo());
                      fC->populateCoreMembers(InvalidNodeId, InvalidNodeId,
                                              pass);
                      assigns->push_back(pass);
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
      Design* design = compileDesign->getCompiler()->getDesign();
      ModuleDefinition* def =
          design->getModuleDefinition(StrCat(libName, "@", typeName));
      if (def) {
        if (def->getType() == VObjectType::paInterface_declaration) {
          interface_typespec* tps = s.MakeInterface_typespec();
          tps->VpiName(typeName);
          tps->Interface_inst(def->getUhdmModel<UHDM::interface_inst>());
          fC->populateCoreMembers(type, type, tps);
          result = tps;
          if (!suffixname.empty()) {
            const DataType* defType = def->getDataType(suffixname);
            bool foundDataType = false;
            while (defType) {
              foundDataType = true;
              if (typespec* t = defType->getTypespec()) {
                result = t;
                return result;
              }
              defType = defType->getDefinition();
            }
            if (foundDataType) {
              // The binding to the actual typespec is still incomplete
              result = s.MakeLogic_typespec();
              return result;
            }
          }
          if (NodeId sub = fC->Sibling(type)) {
            const std::string_view name = fC->SymName(sub);
            if (def->getModPort(name)) {
              interface_typespec* mptps = s.MakeInterface_typespec();
              mptps->VpiName(name);
              mptps->Interface_inst(def->getUhdmModel<UHDM::interface_inst>());
              fC->populateCoreMembers(sub, sub, mptps);
              mptps->VpiParent(tps);
              mptps->VpiIsModPort(true);
              result = mptps;
            }
          }
        }
      }
    }

    if (result == nullptr) {
      unsupported_typespec* tps = s.MakeUnsupported_typespec();
      tps->VpiName(typeName);
      tps->VpiParent(pscope);
      fC->populateCoreMembers(type, type, tps);
      result = tps;
    }
  } else {
    unsupported_typespec* tps = s.MakeUnsupported_typespec();
    tps->VpiName(typeName);
    tps->VpiParent(pscope);
    fC->populateCoreMembers(type, type, tps);
    result = tps;
  }
  return result;
}

UHDM::typespec_member* CompileHelper::buildTypespecMember(
    CompileDesign* compileDesign, const FileContent* fC, NodeId id) {
  /*
  std::string hash = fileName + ":" + name + ":" + value + ":" +
  std::to_string(line) + ":" + std::to_string(column) + ":" +
  std::to_string(eline) + ":" + std::to_string(ecolumn);
  std::unordered_map<std::string, UHDM::typespec_member*>::iterator itr =
      m_cache_typespec_member.find(hash);
  */
  // if (itr == m_cache_typespec_member.end()) {
  Serializer& s = compileDesign->getSerializer();
  typespec_member* var = s.MakeTypespec_member();
  var->VpiName(fC->SymName(id));
  fC->populateCoreMembers(id, id, var);
  //  m_cache_typespec_member.insert(std::make_pair(hash, var));
  //} else {
  //  var = (*itr).second;
  //}
  return var;
}

int_typespec* CompileHelper::buildIntTypespec(
    CompileDesign* compileDesign, PathId fileId, std::string_view name,
    std::string_view value, uint32_t line, uint16_t column, uint32_t eline,
    uint16_t ecolumn) {
  FileSystem* const fileSystem = m_session->getFileSystem();
  /*
  std::string hash = fileName + ":" + name + ":" + value + ":" +
  std::to_string(line)  + ":" + std::to_string(column) + ":" +
  std::to_string(eline) + ":" + std::to_string(ecolumn);
  std::unordered_map<std::string, UHDM::int_typespec*>::iterator itr =
      m_cache_int_typespec.find(hash);
  */
  int_typespec* var = nullptr;
  // if (itr == m_cache_int_typespec.end()) {
  Serializer& s = compileDesign->getSerializer();
  var = s.MakeInt_typespec();
  var->VpiValue(value);
  var->VpiName(name);
  var->VpiFile(fileSystem->toPath(fileId));
  var->VpiLineNo(line);
  var->VpiColumnNo(column);
  var->VpiEndLineNo(eline);
  var->VpiEndColumnNo(ecolumn);
  //  m_cache_int_typespec.insert(std::make_pair(hash, var));
  //} else {
  //  var = (*itr).second;
  //}
  return var;
}

UHDM::typespec* CompileHelper::compileBuiltinTypespec(
    DesignComponent* component, const FileContent* fC, NodeId type,
    VObjectType the_type, CompileDesign* compileDesign, VectorOfrange* ranges,
    UHDM::any* pstmt) {
  UHDM::Serializer& s = compileDesign->getSerializer();
  typespec* result = nullptr;
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
      logic_typespec* var = s.MakeLogic_typespec();
      var->VpiSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? sign : type, var);
      if ((ranges != nullptr) && !ranges->empty()) {
        var->Ranges(ranges);
        for (UHDM::range* r : *ranges) r->VpiParent(var, true);
        var->VpiEndLineNo(ranges->back()->VpiEndLineNo());
        var->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
      }
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Int: {
      int_typespec* var = s.MakeInt_typespec();
      var->VpiSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? type : sign, var);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Integer: {
      integer_typespec* var = s.MakeInteger_typespec();
      var->VpiSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? type : sign, var);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Byte: {
      byte_typespec* var = s.MakeByte_typespec();
      var->VpiSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? type : sign, var);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_LongInt: {
      long_int_typespec* var = s.MakeLong_int_typespec();
      var->VpiSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? type : sign, var);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Shortint: {
      short_int_typespec* var = s.MakeShort_int_typespec();
      var->VpiSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? type : sign, var);
      result = var;
      break;
    }
    case VObjectType::paIntegerAtomType_Time: {
      time_typespec* var = s.MakeTime_typespec();
      fC->populateCoreMembers(type, type, var);
      result = var;
      break;
    }
    case VObjectType::paIntVec_TypeBit: {
      bit_typespec* var = s.MakeBit_typespec();
      if ((ranges != nullptr) && !ranges->empty()) {
        var->Ranges(ranges);
        for (UHDM::range* r : *ranges) r->VpiParent(var, true);
        var->VpiEndLineNo(ranges->back()->VpiEndLineNo());
        var->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
      }
      isSigned = false;
      if (sign && (fC->Type(sign) == VObjectType::paSigning_Signed)) {
        isSigned = true;
      }
      var->VpiSigned(isSigned);
      fC->populateCoreMembers(type, isSigned ? sign : type, var);
      result = var;
      break;
    }
    case VObjectType::paNonIntType_ShortReal: {
      short_real_typespec* var = s.MakeShort_real_typespec();
      fC->populateCoreMembers(type, type, var);
      result = var;
      break;
    }
    case VObjectType::paNonIntType_Real: {
      real_typespec* var = s.MakeReal_typespec();
      fC->populateCoreMembers(type, type, var);
      result = var;
      break;
    }
    case VObjectType::paString_type: {
      UHDM::string_typespec* tps = s.MakeString_typespec();
      fC->populateCoreMembers(type, type, tps);
      result = tps;
      break;
    }
    default:
      logic_typespec* var = s.MakeLogic_typespec();
      if ((ranges != nullptr) && !ranges->empty()) {
        var->Ranges(ranges);
        for (UHDM::range* r : *ranges) r->VpiParent(var, true);
        var->VpiEndLineNo(ranges->back()->VpiEndLineNo());
        var->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
      }
      fC->populateCoreMembers(type, type, var);
      result = var;
      break;
  }
  result->VpiParent(pstmt);
  return result;
}

UHDM::typespec* CompileHelper::compileTypespec(
    DesignComponent* component, const FileContent* fC, NodeId id,
    CompileDesign* compileDesign, Reduce reduce, UHDM::any* pstmt,
    SURELOG::ValuedComponentI* instance, bool isVariable) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  ErrorContainer* const errors = m_session->getErrorContainer();

  UHDM::Serializer& s = compileDesign->getSerializer();
  if (pstmt == nullptr) pstmt = component->getUhdmModel();
  if (pstmt == nullptr)
    pstmt = compileDesign->getCompiler()->getDesign()->getUhdmDesign();
  NodeId nodeId = id;
  if (fC->Type(id) == VObjectType::paData_type) nodeId = fC->Child(id);
  UHDM::typespec* result = nullptr;
  NodeId type = nodeId;
  VObjectType the_type = fC->Type(type);
  if (the_type == VObjectType::paData_type_or_implicit) {
    type = fC->Child(type);
    the_type = fC->Type(type);
  }
  if (the_type == VObjectType::paData_type) {
    if (fC->Child(type)) {
      type = fC->Child(type);
      if (fC->Type(type) == VObjectType::paVIRTUAL) type = fC->Sibling(type);
    } else {
      // Implicit type
    }
    the_type = fC->Type(type);
  }
  NodeId Packed_dimension;
  if (the_type == VObjectType::paPacked_dimension) {
    Packed_dimension = type;
  } else if (the_type == VObjectType::slStringConst) {
    // Class parameter or struct reference
    Packed_dimension = fC->Sibling(type);
    if (fC->Type(Packed_dimension) != VObjectType::paPacked_dimension)
      Packed_dimension = InvalidNodeId;
  } else {
    Packed_dimension = fC->Sibling(type);
    if (fC->Type(Packed_dimension) == VObjectType::paData_type_or_implicit) {
      Packed_dimension = fC->Child(Packed_dimension);
    }
  }
  bool isPacked = false;
  if (fC->Type(Packed_dimension) == VObjectType::paPacked_keyword) {
    Packed_dimension = fC->Sibling(Packed_dimension);
    isPacked = true;
  }
  if (fC->Type(Packed_dimension) == VObjectType::paStruct_union_member ||
      fC->Type(Packed_dimension) == VObjectType::slStringConst) {
    Packed_dimension = fC->Sibling(Packed_dimension);
  }

  if (fC->Type(Packed_dimension) == VObjectType::paSigning_Signed ||
      fC->Type(Packed_dimension) == VObjectType::paSigning_Unsigned) {
    Packed_dimension = fC->Sibling(Packed_dimension);
  }
  NodeId Packed_dimensionStartId, Packed_dimensionEndId;
  if (fC->Type(Packed_dimension) == VObjectType::paPacked_dimension) {
    isPacked = true;
    Packed_dimensionStartId = Packed_dimensionEndId = Packed_dimension;
    while (fC->Sibling(Packed_dimensionEndId))
      Packed_dimensionEndId = fC->Sibling(Packed_dimensionEndId);
  }
  int32_t size;
  VectorOfrange* ranges =
      compileRanges(component, fC, Packed_dimension, compileDesign, reduce,
                    pstmt, instance, size, false);
  switch (the_type) {
    case VObjectType::paConstant_mintypmax_expression:
    case VObjectType::paConstant_primary: {
      return compileTypespec(component, fC, fC->Child(type), compileDesign,
                             reduce, pstmt, instance, false);
    }
    case VObjectType::paSystem_task: {
      if (UHDM::any* res = compileExpression(component, fC, type, compileDesign,
                                             reduce, pstmt, instance)) {
        integer_typespec* var = s.MakeInteger_typespec();
        fC->populateCoreMembers(type, type, var);
        result = var;
        if (UHDM::constant* constant = any_cast<UHDM::constant*>(res)) {
          var->VpiValue(constant->VpiValue());
        } else {
          var->Expr((expr*)res);
        }
      } else {
        unsupported_typespec* tps = s.MakeUnsupported_typespec();
        tps->VpiParent(pstmt);
        fC->populateCoreMembers(type, type, tps);
        result = tps;
      }
      break;
    }
    case VObjectType::paEnum_base_type:
    case VObjectType::paEnum_name_declaration: {
      typespec* baseType = nullptr;
      uint64_t baseSize = 64;
      enum_typespec* en = s.MakeEnum_typespec();
      if (the_type == VObjectType::paEnum_base_type) {
        baseType =
            compileTypespec(component, fC, fC->Child(type), compileDesign,
                            reduce, pstmt, instance, isVariable);
        type = fC->Sibling(type);
        bool invalidValue = false;
        baseSize =
            Bits(baseType, invalidValue, component, compileDesign, reduce,
                 instance, fC->getFileId(), baseType->VpiLineNo(), true);
        ref_typespec* baseTypeRef = s.MakeRef_typespec();
        baseTypeRef->VpiParent(en);
        baseTypeRef->VpiName(fC->SymName(nodeId));
        baseTypeRef->Actual_typespec(baseType);
        fC->populateCoreMembers(nodeId, nodeId, baseTypeRef);
        en->Base_typespec(baseTypeRef);
      }
      NodeId dataTypeId = nodeId;
      while (dataTypeId && (fC->Type(dataTypeId) != VObjectType::paData_type)) {
        dataTypeId = fC->Parent(dataTypeId);
      }
      en->VpiName(fC->SymName(nodeId));
      fC->populateCoreMembers(dataTypeId, dataTypeId, en);
      VectorOfenum_const* econsts = en->Enum_consts(true);
      NodeId enum_name_declaration = type;
      int32_t val = 0;
      while (enum_name_declaration) {
        NodeId enumNameId = fC->Child(enum_name_declaration);
        const std::string_view enumName = fC->SymName(enumNameId);
        NodeId enumValueId = fC->Sibling(enumNameId);
        Value* value = nullptr;
        if (enumValueId) {
          value = m_exprBuilder.evalExpr(fC, enumValueId, component);
          value->setValid();
        } else {
          value = m_exprBuilder.getValueFactory().newLValue();
          value->set(val, Value::Type::Integer, baseSize);
        }
        // the_enum->addValue(enumName, fC->Line(enumNameId), value);
        val++;
        if (component) component->setValue(enumName, value, m_exprBuilder);
        Variable* variable =
            new Variable(nullptr, fC, enumValueId, InvalidNodeId, enumName);
        if (component) component->addVariable(variable);

        enum_const* econst = s.MakeEnum_const();
        econst->VpiName(enumName);
        econst->VpiParent(en);
        fC->populateCoreMembers(enum_name_declaration, enum_name_declaration,
                                econst);
        econst->VpiValue(value->uhdmValue());
        if (enumValueId) {
          any* exp =
              compileExpression(component, fC, enumValueId, compileDesign,
                                Reduce::No, pstmt, nullptr);
          UHDM::ExprEval eval;
          econst->VpiDecompile(eval.prettyPrint(exp));
        } else {
          econst->VpiDecompile(value->decompiledValue());
        }
        econst->VpiSize(value->getSize());
        econsts->push_back(econst);
        enum_name_declaration = fC->Sibling(enum_name_declaration);
      }
      result = en;
      break;
    }
    case VObjectType::paInterface_identifier: {
      interface_typespec* tps = s.MakeInterface_typespec();
      NodeId Name = fC->Child(type);
      const std::string_view name = fC->SymName(Name);
      tps->VpiName(name);
      fC->populateCoreMembers(type, type, tps);
      result = tps;
      break;
    }
    case VObjectType::paSigning_Signed: {
      if (m_elaborate == Elaborate::Yes) {
        if (isVariable) {
          // 6.8 Variable declarations, implicit type
          logic_typespec* tps = s.MakeLogic_typespec();
          tps->VpiSigned(true);
          if ((ranges != nullptr) && !ranges->empty()) {
            tps->Ranges(ranges);
            for (UHDM::range* r : *ranges) r->VpiParent(tps, true);
            tps->VpiEndLineNo(ranges->back()->VpiEndLineNo());
            tps->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
          }
          result = tps;
        } else {
          // Parameter implicit type is int
          int_typespec* tps = s.MakeInt_typespec();
          tps->VpiSigned(true);
          fC->populateCoreMembers(type, type, tps);
          if ((ranges != nullptr) && !ranges->empty()) {
            tps->Ranges(ranges);
            for (UHDM::range* r : *ranges) r->VpiParent(tps, true);
            tps->VpiEndLineNo(ranges->back()->VpiEndLineNo());
            tps->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
          }
          result = tps;
        }
      }
      break;
    }
    case VObjectType::paSigning_Unsigned: {
      if (m_elaborate == Elaborate::Yes) {
        if (isVariable) {
          // 6.8 Variable declarations, implicit type
          logic_typespec* tps = s.MakeLogic_typespec();
          if ((ranges != nullptr) && !ranges->empty()) {
            tps->Ranges(ranges);
            for (UHDM::range* r : *ranges) r->VpiParent(tps, true);
            tps->VpiEndLineNo(ranges->back()->VpiEndLineNo());
            tps->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
          }
          result = tps;
        } else {
          // Parameter implicit type is int
          int_typespec* tps = s.MakeInt_typespec();
          if ((ranges != nullptr) && !ranges->empty()) {
            tps->Ranges(ranges);
            for (UHDM::range* r : *ranges) r->VpiParent(tps, true);
            tps->VpiEndLineNo(ranges->back()->VpiEndLineNo());
            tps->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
          }
          result = tps;
        }
      }
      break;
    }
    case VObjectType::paPacked_dimension: {
      if (m_elaborate == Elaborate::Yes) {
        if (isVariable) {
          // 6.8 Variable declarations, implicit type
          logic_typespec* tps = s.MakeLogic_typespec();
          if ((ranges != nullptr) && !ranges->empty()) {
            tps->Ranges(ranges);
            for (UHDM::range* r : *ranges) r->VpiParent(tps, true);
            tps->VpiEndLineNo(ranges->back()->VpiEndLineNo());
            tps->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
          }
          result = tps;
        } else {
          // Parameter implicit type is bit
          int_typespec* tps = s.MakeInt_typespec();
          if ((ranges != nullptr) && !ranges->empty()) {
            tps->Ranges(ranges);
            for (UHDM::range* r : *ranges) r->VpiParent(tps, true);
            tps->VpiEndLineNo(ranges->back()->VpiEndLineNo());
            tps->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
          }
          result = tps;
        }
        fC->populateCoreMembers(type, type, result);
      }
      break;
    }
    case VObjectType::paExpression: {
      NodeId Primary = fC->Child(type);
      NodeId Primary_literal = fC->Child(Primary);
      NodeId Name = fC->Child(Primary_literal);
      if (fC->Type(Name) == VObjectType::paClass_scope) {
        return compileTypespec(component, fC, Name, compileDesign, reduce,
                               pstmt, instance, isVariable);
      }
      if (instance) {
        const std::string_view name = fC->SymName(Name);
        result = (typespec*)bindTypespec(name, instance, s);
      }
      break;
    }
    case VObjectType::paPrimary_literal: {
      NodeId literal = fC->Child(type);
      if (fC->Type(literal) == VObjectType::slStringConst) {
        const std::string_view typeName = fC->SymName(literal);
        result = compileDatastructureTypespec(
            component, fC, type, compileDesign, reduce, instance, "", typeName);
      } else {
        integer_typespec* var = s.MakeInteger_typespec();
        var->VpiValue(StrCat("INT:", fC->SymName(literal)));
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
                                      compileDesign, ranges, pstmt);
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
      Package* pack =
          compileDesign->getCompiler()->getDesign()->getPackage(packageName);
      if (pack) {
        const DataType* dtype = pack->getDataType(name);
        if (dtype == nullptr) {
          ClassDefinition* classDefn = pack->getClassDefinition(name);
          dtype = (const DataType*)classDefn;
          if (dtype) {
            class_typespec* ref = s.MakeClass_typespec();
            ref->Class_defn(classDefn->getUhdmModel<UHDM::class_defn>());
            ref->VpiName(typeName);
            fC->populateCoreMembers(type, type, ref);
            result = ref;
            break;
          }
        }
        while (dtype) {
          const TypeDef* typed = datatype_cast<const TypeDef*>(dtype);
          if (typed) {
            const DataType* dt = typed->getDataType();
            if (const Enum* en = datatype_cast<const Enum*>(dt)) {
              result = en->getTypespec();
            } else if (const Struct* st = datatype_cast<const Struct*>(dt)) {
              result = st->getTypespec();
            } else if (const Union* un = datatype_cast<const Union*>(dt)) {
              result = un->getTypespec();
            } else if (const SimpleType* sit =
                           datatype_cast<const SimpleType*>(dt)) {
              result = sit->getTypespec();
            } else if (const DummyType* sit =
                           datatype_cast<const DummyType*>(dt)) {
              result = sit->getTypespec();
            }
          }
          dtype = dtype->getDefinition();
          if (result) {
            break;
          }
        }
        if (!result) {
          UHDM::VectorOfparam_assign* param_assigns = pack->getParam_assigns();
          if (param_assigns) {
            for (param_assign* param : *param_assigns) {
              const std::string_view param_name = param->Lhs()->VpiName();
              if (param_name == name) {
                const any* rhs = param->Rhs();
                if (const expr* exp = any_cast<const expr*>(rhs)) {
                  UHDM::int_typespec* its = s.MakeInt_typespec();
                  its->VpiValue(exp->VpiValue());
                  result = its;
                } else {
                  result = (UHDM::typespec*)rhs;
                }
                break;
              }
            }
          }
        }
        if (ranges && result) {
          if ((result->UhdmType() != uhdmlogic_typespec) &&
              (result->UhdmType() != uhdmbit_typespec) &&
              (result->UhdmType() != uhdmint_typespec)) {
            ref_typespec* resultRef = s.MakeRef_typespec();
            fC->populateCoreMembers(type, type, resultRef);
            resultRef->Actual_typespec(result);
            if (isPacked) {
              packed_array_typespec* pats = s.MakePacked_array_typespec();
              pats->Elem_typespec(resultRef);
              resultRef->VpiParent(pats);
              if (ranges != nullptr) {
                pats->Ranges(ranges);
                for (UHDM::range* r : *ranges) r->VpiParent(pats, true);
              }
              fC->populateCoreMembers(Packed_dimensionStartId,
                                      Packed_dimensionEndId, pats);
              result = pats;
            } else {
              array_typespec* pats = s.MakeArray_typespec();
              pats->Elem_typespec(resultRef);
              resultRef->VpiParent(pats);
              if (ranges != nullptr) {
                pats->Ranges(ranges);
                for (UHDM::range* r : *ranges) r->VpiParent(pats, true);
              }
              result = pats;
            }
            fC->populateCoreMembers(Packed_dimension, Packed_dimension, result);
          }
        }
      }
      if (result == nullptr) {
        unsupported_typespec* ref = s.MakeUnsupported_typespec();
        ref->VpiParent(pstmt);
        ref->VpiPacked(isPacked);
        ref->VpiName(typeName);
        fC->populateCoreMembers(id, id, ref);
        if (ranges != nullptr) {
          ref->Ranges(ranges);
          for (UHDM::range* r : *ranges) r->VpiParent(ref, true);
        }
        result = ref;
      }
      break;
    }
    case VObjectType::paStruct_union: {
      NodeId struct_or_union = fC->Child(type);
      VObjectType struct_or_union_type = fC->Type(struct_or_union);
      VectorOftypespec_member* members = s.MakeTypespec_memberVec();

      NodeId struct_or_union_member = fC->Sibling(type);
      if (fC->Type(struct_or_union_member) == VObjectType::paPacked_keyword) {
        struct_or_union_member = fC->Sibling(struct_or_union_member);
        isPacked = true;
      }

      typespec* structOtUnionTypespec = nullptr;
      if (struct_or_union_type == VObjectType::paStruct_keyword) {
        struct_typespec* ts = s.MakeStruct_typespec();
        ts->VpiPacked(isPacked);
        ts->Members(members);
        result = structOtUnionTypespec = ts;
      } else {
        union_typespec* ts = s.MakeUnion_typespec();
        ts->VpiPacked(isPacked);
        ts->Members(members);
        result = structOtUnionTypespec = ts;
      }
      result->VpiParent(pstmt);
      fC->populateCoreMembers(id, id, result);

      if (ranges) {
        structOtUnionTypespec->VpiEndLineNo(fC->Line(Packed_dimensionStartId));
        structOtUnionTypespec->VpiEndColumnNo(
            fC->Column(Packed_dimensionStartId) - 1);

        ref_typespec* resultRef = s.MakeRef_typespec();
        resultRef->Actual_typespec(result);
        fC->populateCoreMembers(id, InvalidNodeId, resultRef);
        resultRef->VpiEndLineNo(fC->Line(Packed_dimensionStartId));
        resultRef->VpiEndColumnNo(fC->Column(Packed_dimensionStartId) - 1);
        if (isPacked) {
          packed_array_typespec* pats = s.MakePacked_array_typespec();
          pats->Elem_typespec(resultRef);
          if (ranges != nullptr) {
            pats->Ranges(ranges);
            for (UHDM::range* r : *ranges) r->VpiParent(pats, true);
          }
          resultRef->VpiParent(pats);
          result = pats;
        } else {
          array_typespec* pats = s.MakeArray_typespec();
          pats->Elem_typespec(resultRef);
          if (ranges != nullptr) {
            pats->Ranges(ranges);
            for (UHDM::range* r : *ranges) r->VpiParent(pats, true);
          }
          resultRef->VpiParent(pats);
          result = pats;
        }
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
          typespec* member_ts = nullptr;
          if (Data_type) {
            member_ts = compileTypespec(component, fC, Data_type, compileDesign,
                                        reduce, result, instance, false);
          } else {
            void_typespec* tps = s.MakeVoid_typespec();
            tps->VpiParent(result);
            fC->populateCoreMembers(Data_type_or_void, Data_type_or_void, tps);
            member_ts = tps;
          }
          NodeId member_name = fC->Child(Variable_decl_assignment);
          NodeId Expression = fC->Sibling(member_name);
          typespec_member* m =
              buildTypespecMember(compileDesign, fC, member_name);
          m->VpiParent(structOtUnionTypespec);
          if (member_ts != nullptr) {
            if (m->Typespec() == nullptr) {
              ref_typespec* tsRef = s.MakeRef_typespec();
              tsRef->VpiParent(m);
              tsRef->VpiName(fC->SymName(Data_type));
              fC->populateCoreMembers(Data_type_or_void, Data_type_or_void,
                                      tsRef);
              m->Typespec(tsRef);
            }
            m->Typespec()->Actual_typespec(member_ts);
          }
          if (Expression &&
              (fC->Type(Expression) != VObjectType::paVariable_dimension)) {
            if (any* ex =
                    compileExpression(component, fC, Expression, compileDesign,
                                      reduce, nullptr, instance, false)) {
              m->Default_value((expr*)ex);
            }
          }
          if (Expression &&
              (fC->Type(Expression) == VObjectType::paVariable_dimension)) {
            NodeId Unpacked_dimension = fC->Child(Expression);
            if (fC->Type(Unpacked_dimension) ==
                VObjectType::paUnpacked_dimension) {
              int32_t size;
              VectorOfrange* ranges = compileRanges(
                  component, fC, Unpacked_dimension, compileDesign, reduce,
                  nullptr, instance, size, false);
              array_typespec* pats = s.MakeArray_typespec();
              ref_typespec* ref = s.MakeRef_typespec();
              if (isPacked) {
                Location loc1(
                    fC->getFileId(), fC->Line(Unpacked_dimension),
                    fC->Column(Unpacked_dimension),
                    symbols->registerSymbol(fC->SymName(member_name)));
                Error err(ErrorDefinition::COMP_UNPACKED_IN_PACKED, loc1);
                errors->addError(err);
              }
              pats->Elem_typespec(ref);
              pats->VpiParent(m);
              fC->populateCoreMembers(Unpacked_dimension, Unpacked_dimension,
                                      pats);
              ref->VpiParent(m);
              fC->populateCoreMembers(Data_type, Data_type, ref);
              ref->Actual_typespec(m->Typespec()->Actual_typespec());
              m->Typespec()->Actual_typespec(pats);
              if (ranges != nullptr) {
                pats->Ranges(ranges);
                for (auto r : *ranges) r->VpiParent(pats, true);
              }
            }
          }
          members->push_back(m);
          Variable_decl_assignment = fC->Sibling(Variable_decl_assignment);
        }
        struct_or_union_member = fC->Sibling(struct_or_union_member);
      }
      break;
    }
    case VObjectType::paSimple_type:
    case VObjectType::paPs_type_identifier:
    case VObjectType::paInteger_type: {
      return compileTypespec(component, fC, fC->Child(type), compileDesign,
                             reduce, pstmt, instance, false);
    }
    case VObjectType::slStringConst: {
      const std::string_view typeName = fC->SymName(type);
      if (typeName == "logic") {
        logic_typespec* var = s.MakeLogic_typespec();
        if ((ranges != nullptr) && !ranges->empty()) {
          var->Ranges(ranges);
          for (UHDM::range* r : *ranges) r->VpiParent(var, true);
          var->VpiEndLineNo(ranges->back()->VpiEndLineNo());
          var->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
        }
        fC->populateCoreMembers(type, type, var);
        result = var;
      } else if (typeName == "bit") {
        bit_typespec* var = s.MakeBit_typespec();
        if ((ranges != nullptr) && !ranges->empty()) {
          var->Ranges(ranges);
          for (UHDM::range* r : *ranges) r->VpiParent(var, true);
          var->VpiEndLineNo(ranges->back()->VpiEndLineNo());
          var->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
        }
        fC->populateCoreMembers(type, type, var);
        result = var;
      } else if (typeName == "byte") {
        byte_typespec* var = s.MakeByte_typespec();
        fC->populateCoreMembers(type, type, var);
        result = var;
      } else if ((m_reduce == Reduce::Yes) && (reduce == Reduce::Yes)) {
        if (any* cast_to =
                getValue(typeName, component, compileDesign,
                         reduce == Reduce::Yes ? Reduce::No : Reduce::Yes,
                         instance, fC->getFileId(), fC->Line(type), nullptr)) {
          constant* c = any_cast<constant*>(cast_to);
          if (c) {
            integer_typespec* var = s.MakeInteger_typespec();
            var->VpiValue(c->VpiValue());
            fC->populateCoreMembers(type, type, var);
            result = var;
          } else {
            void_typespec* tps = s.MakeVoid_typespec();
            fC->populateCoreMembers(type, type, tps);
            result = tps;
          }
        }
      }
      if (!result) {
        while (instance) {
          if (ModuleInstance* inst =
                  valuedcomponenti_cast<ModuleInstance*>(instance)) {
            if (inst->getNetlist()) {
              UHDM::VectorOfparam_assign* param_assigns =
                  inst->getNetlist()->param_assigns();
              if (param_assigns) {
                for (param_assign* param : *param_assigns) {
                  const std::string_view param_name = param->Lhs()->VpiName();
                  if (param_name == typeName) {
                    const any* rhs = param->Rhs();
                    if (const constant* exp = any_cast<const constant*>(rhs)) {
                      int_typespec* its = buildIntTypespec(
                          compileDesign,
                          fileSystem->toPathId(param->VpiFile(), symbols),
                          typeName, exp->VpiValue(), param->VpiLineNo(),
                          param->VpiColumnNo(), param->VpiLineNo(),
                          param->VpiColumnNo());
                      result = its;
                    } else {
                      any* ex =
                          compileExpression(component, fC, type, compileDesign,
                                            Reduce::No, pstmt, instance, false);
                      if (ex) {
                        hier_path* path = nullptr;
                        if (ex->UhdmType() == uhdmhier_path) {
                          path = (hier_path*)ex;
                        } else if (ex->UhdmType() == uhdmref_obj) {
                          path = s.MakeHier_path();
                          ref_obj* ref = s.MakeRef_obj();
                          ref->VpiName(typeName);
                          ref->VpiParent(path);
                          fC->populateCoreMembers(type, type, ref);
                          path->Path_elems(true)->push_back(ref);
                        }
                        if (path) {
                          bool invalidValue = false;
                          result = (typespec*)decodeHierPath(
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
        if (UHDM::VectorOfparam_assign* param_assigns =
                component->getParam_assigns()) {
          for (param_assign* param : *param_assigns) {
            const std::string_view param_name = param->Lhs()->VpiName();
            if (param_name == typeName) {
              const any* rhs = param->Rhs();
              if (const constant* exp = any_cast<const constant*>(rhs)) {
                int_typespec* its = buildIntTypespec(
                    compileDesign,
                    fileSystem->toPathId(param->VpiFile(), symbols), typeName,
                    exp->VpiValue(), param->VpiLineNo(), param->VpiColumnNo(),
                    param->VpiLineNo(), param->VpiColumnNo());
                result = its;
              } else if (const operation* exp =
                             any_cast<const operation*>(rhs)) {
                if (const ref_typespec* rt = exp->Typespec())
                  result = const_cast<typespec*>(rt->Actual_typespec());
              }
              break;
            }
          }
        }
      }
      if (!result) {
        if (component) {
          Design* design = compileDesign->getCompiler()->getDesign();
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
            class_typespec* tps = s.MakeClass_typespec();
            tps->VpiName(typeName);
            tps->VpiParent(pstmt);
            tps->Class_defn(cl->getUhdmModel<UHDM::class_defn>());
            fC->populateCoreMembers(type, type, tps);
            result = tps;
          }
        }
      }
      if (result == nullptr) {
        result = compileDatastructureTypespec(
            component, fC, type, compileDesign, reduce, instance, "", typeName);
        if (ranges && result) {
          UHDM_OBJECT_TYPE dstype = result->UhdmType();
          ref_typespec* resultRef = s.MakeRef_typespec();
          resultRef->VpiName(typeName);
          resultRef->Actual_typespec(result);
          if (dstype == uhdmstruct_typespec || dstype == uhdmenum_typespec ||
              dstype == uhdmunion_typespec) {
            packed_array_typespec* pats = s.MakePacked_array_typespec();
            pats->VpiParent(pstmt);
            pats->Elem_typespec(resultRef);
            pats->Ranges(ranges);
            result = pats;
          } else if (dstype == uhdmlogic_typespec) {
            logic_typespec* pats = s.MakeLogic_typespec();
            pats->VpiParent(pstmt);
            pats->Elem_typespec(resultRef);
            pats->Ranges(ranges);
            result = pats;
          } else if (dstype == uhdmarray_typespec ||
                     dstype == uhdminterface_typespec) {
            array_typespec* pats = s.MakeArray_typespec();
            pats->VpiParent(pstmt);
            pats->Elem_typespec(resultRef);
            pats->Ranges(ranges);
            result = pats;
          } else if (dstype == uhdmpacked_array_typespec) {
            packed_array_typespec* pats = s.MakePacked_array_typespec();
            pats->VpiParent(pstmt);
            pats->Elem_typespec(resultRef);
            pats->Ranges(ranges);
            result = pats;
          }
          resultRef->VpiParent(result);
          for (UHDM::range* r : *ranges) r->VpiParent(result, true);
          fC->populateCoreMembers(Packed_dimensionStartId,
                                  Packed_dimensionEndId, result);
          result->VpiEndLineNo(ranges->back()->VpiEndLineNo());
          result->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
          fC->populateCoreMembers(type, type, resultRef);
        }
        if (result) {
          fC->populateCoreMembers(type, type, result);
        }
      }
      if ((!result) && component) {
        if (UHDM::VectorOfany* params = component->getParameters()) {
          for (any* param : *params) {
            if ((param->UhdmType() == uhdmtype_parameter) &&
                (param->VpiName() == typeName)) {
              result = (typespec*)param;
              break;
            }
          }
        }
      }

      break;
    }
    case VObjectType::paConstant_expression: {
      if (expr* exp = (expr*)compileExpression(
              component, fC, type, compileDesign, reduce, pstmt, instance,
              reduce == Reduce::No)) {
        if (exp->UhdmType() == uhdmref_obj) {
          return compileTypespec(component, fC, fC->Child(type), compileDesign,
                                 reduce, result, instance, false);
        } else {
          integer_typespec* var = s.MakeInteger_typespec();
          if (exp->UhdmType() == uhdmconstant) {
            var->VpiValue(exp->VpiValue());
          } else {
            var->Expr(exp);
            exp->VpiParent(var, true);
          }
          fC->populateCoreMembers(type, type, var);
          result = var;
        }
      }
      break;
    }
    case VObjectType::paChandle_type: {
      UHDM::chandle_typespec* tps = s.MakeChandle_typespec();
      fC->populateCoreMembers(type, type, tps);
      result = tps;
      break;
    }
    case VObjectType::paConstant_range: {
      UHDM::logic_typespec* tps = s.MakeLogic_typespec();
      fC->populateCoreMembers(type, type, tps);
      if (VectorOfrange* ranges =
              compileRanges(component, fC, type, compileDesign, reduce, tps,
                            instance, size, false)) {
        if (!ranges->empty()) {
          tps->Ranges(ranges);
          for (UHDM::range* r : *ranges) r->VpiParent(tps, true);
          tps->VpiEndLineNo(ranges->back()->VpiEndLineNo());
          tps->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
        }
      }
      result = tps;
      break;
    }
    case VObjectType::paEvent_type: {
      UHDM::event_typespec* tps = s.MakeEvent_typespec();
      fC->populateCoreMembers(type, type, tps);
      result = tps;
      break;
    }
    case VObjectType::paNonIntType_RealTime: {
      UHDM::time_typespec* tps = s.MakeTime_typespec();
      fC->populateCoreMembers(type, type, tps);
      result = tps;
      break;
    }
    case VObjectType::paType_reference: {
      NodeId child = fC->Child(type);
      if (fC->Type(child) == VObjectType::paExpression) {
        expr* exp = (expr*)compileExpression(component, fC, child,
                                             compileDesign, reduce, nullptr,
                                             instance, reduce == Reduce::Yes);
        if (exp) {
          UHDM_OBJECT_TYPE typ = exp->UhdmType();
          if (typ == uhdmref_obj) {
            return compileTypespec(component, fC, child, compileDesign, reduce,
                                   result, instance, false);
          } else if (typ == uhdmconstant) {
            constant* c = (constant*)exp;
            int32_t ctype = c->VpiConstType();
            if (ctype == vpiIntConst || ctype == vpiDecConst) {
              int_typespec* tps = s.MakeInt_typespec();
              tps->VpiSigned(true);
              result = tps;
            } else if (ctype == vpiUIntConst || ctype == vpiBinaryConst ||
                       ctype == vpiHexConst || ctype == vpiOctConst) {
              int_typespec* tps = s.MakeInt_typespec();
              result = tps;
            } else if (ctype == vpiRealConst) {
              real_typespec* tps = s.MakeReal_typespec();
              result = tps;
            } else if (ctype == vpiStringConst) {
              string_typespec* tps = s.MakeString_typespec();
              result = tps;
            } else if (ctype == vpiTimeConst) {
              time_typespec* tps = s.MakeTime_typespec();
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
        return compileTypespec(component, fC, child, compileDesign, reduce,
                               result, instance, false);
      }
      break;
    }
    case VObjectType::paData_type_or_implicit: {
      logic_typespec* tps = s.MakeLogic_typespec();
      fC->populateCoreMembers(type, type, tps);
      if (VectorOfrange* ranges =
              compileRanges(component, fC, type, compileDesign, reduce, tps,
                            instance, size, false)) {
        if (!ranges->empty()) {
          tps->Ranges(ranges);
          for (UHDM::range* r : *ranges) r->VpiParent(tps, true);
          tps->VpiEndLineNo(ranges->back()->VpiEndLineNo());
          tps->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
        }
      }
      result = tps;
      break;
    }
    case VObjectType::paImplicit_data_type: {
      // Interconnect
      logic_typespec* tps = s.MakeLogic_typespec();
      fC->populateCoreMembers(type, type, tps);
      if (VectorOfrange* ranges =
              compileRanges(component, fC, fC->Child(type), compileDesign,
                            reduce, tps, instance, size, false)) {
        if (!ranges->empty()) {
          tps->Ranges(ranges);
          for (UHDM::range* r : *ranges) r->VpiParent(tps, true);
          tps->VpiEndLineNo(ranges->back()->VpiEndLineNo());
          tps->VpiEndColumnNo(ranges->back()->VpiEndColumnNo());
        }
      }
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
  if (result) {
    if ((m_elaborate == Elaborate::Yes) && component && !result->Instance()) {
      result->Instance(component->getUhdmModel<UHDM::instance>());
    }
    if (component->getUhdmModel() != nullptr) {
      result->VpiParent(component->getUhdmModel());
    }
  }
  return result;
}

UHDM::typespec* CompileHelper::elabTypespec(DesignComponent* component,
                                            UHDM::typespec* spec,
                                            CompileDesign* compileDesign,
                                            UHDM::any* pexpr,
                                            ValuedComponentI* instance) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();

  Serializer& s = compileDesign->getSerializer();
  typespec* result = spec;
  UHDM_OBJECT_TYPE type = spec->UhdmType();
  VectorOfrange* ranges = nullptr;
  switch (type) {
    case uhdmbit_typespec: {
      bit_typespec* tps = (bit_typespec*)spec;
      ranges = tps->Ranges();
      if (ranges) {
        ElaboratorContext elaboratorContext(&s, false, true);
        bit_typespec* res = any_cast<bit_typespec*>(
            UHDM::clone_tree((any*)spec, &elaboratorContext));
        ranges = res->Ranges();
        result = res;
      }
      break;
    }
    case uhdmlogic_typespec: {
      logic_typespec* tps = (logic_typespec*)spec;
      ranges = tps->Ranges();
      if (ranges) {
        ElaboratorContext elaboratorContext(&s, false, true);
        logic_typespec* res = any_cast<logic_typespec*>(
            UHDM::clone_tree((any*)spec, &elaboratorContext));
        ranges = res->Ranges();
        result = res;
      }
      break;
    }
    case uhdmarray_typespec: {
      array_typespec* tps = (array_typespec*)spec;
      ranges = tps->Ranges();
      if (ranges) {
        ElaboratorContext elaboratorContext(&s, false, true);
        array_typespec* res = any_cast<array_typespec*>(
            UHDM::clone_tree((any*)spec, &elaboratorContext));
        ranges = res->Ranges();
        result = res;
      }
      break;
    }
    case uhdmpacked_array_typespec: {
      packed_array_typespec* tps = (packed_array_typespec*)spec;
      ranges = tps->Ranges();
      if (ranges) {
        ElaboratorContext elaboratorContext(&s, false, true);
        packed_array_typespec* res = any_cast<packed_array_typespec*>(
            UHDM::clone_tree((any*)spec, &elaboratorContext));
        ranges = res->Ranges();
        result = res;
      }
      break;
    }
    default:
      break;
  }
  if ((m_reduce == Reduce::Yes) && ranges) {
    for (UHDM::range* oldRange : *ranges) {
      expr* oldLeft = oldRange->Left_expr();
      expr* oldRight = oldRange->Right_expr();
      bool invalidValue = false;
      expr* newLeft =
          reduceExpr(oldLeft, invalidValue, component, compileDesign, instance,
                     fileSystem->toPathId(oldLeft->VpiFile(), symbols),
                     oldLeft->VpiLineNo(), pexpr);
      expr* newRight =
          reduceExpr(oldRight, invalidValue, component, compileDesign, instance,
                     fileSystem->toPathId(oldRight->VpiFile(), symbols),
                     oldRight->VpiLineNo(), pexpr);
      if (!invalidValue) {
        oldRange->Left_expr(newLeft);
        oldRange->Right_expr(newRight);
      }
    }
  }
  return result;
}

bool CompileHelper::isOverloaded(const UHDM::any* expr,
                                 CompileDesign* compileDesign,
                                 ValuedComponentI* instance) {
  if (instance == nullptr) return false;
  ModuleInstance* inst = valuedcomponenti_cast<ModuleInstance*>(instance);
  if (inst == nullptr) return false;
  std::stack<const any*> stack;
  const UHDM::any* tmp = expr;
  stack.push(tmp);
  while (!stack.empty()) {
    tmp = stack.top();
    stack.pop();
    UHDM_OBJECT_TYPE type = tmp->UhdmType();
    switch (type) {
      case uhdmrange: {
        range* r = (range*)tmp;
        stack.push(r->Left_expr());
        stack.push(r->Right_expr());
        break;
      }
      case uhdmconstant: {
        if (const ref_typespec* rt = ((constant*)tmp)->Typespec()) {
          if (const any* tp = rt->Actual_typespec()) {
            stack.push(tp);
          }
        }
        break;
      }
      case uhdmtypedef_typespec: {
        stack.push(tmp);
        break;
      }
      case uhdmlogic_typespec: {
        logic_typespec* tps = (logic_typespec*)tmp;
        if (tps->Ranges()) {
          for (auto op : *tps->Ranges()) {
            stack.push(op);
          }
        }
        break;
      }
      case uhdmbit_typespec: {
        bit_typespec* tps = (bit_typespec*)tmp;
        if (tps->Ranges()) {
          for (auto op : *tps->Ranges()) {
            stack.push(op);
          }
        }
        break;
      }
      case uhdmarray_typespec: {
        array_typespec* tps = (array_typespec*)tmp;
        if (tps->Ranges()) {
          for (auto op : *tps->Ranges()) {
            stack.push(op);
          }
        }
        if (const ref_typespec* rt = tps->Elem_typespec()) {
          if (const any* etps = rt->Actual_typespec()) {
            stack.push(etps);
          }
        }
        break;
      }
      case uhdmpacked_array_typespec: {
        packed_array_typespec* tps = (packed_array_typespec*)tmp;
        if (tps->Ranges()) {
          for (auto op : *tps->Ranges()) {
            stack.push(op);
          }
        }
        if (const ref_typespec* rt = tps->Elem_typespec()) {
          if (const any* etps = rt->Actual_typespec()) {
            stack.push(etps);
          }
        }
        break;
      }
      case uhdmparameter:
      case uhdmref_obj:
      case uhdmtype_parameter: {
        if (inst->isOverridenParam(tmp->VpiName())) return true;
        break;
      }
      case uhdmoperation: {
        operation* oper = (operation*)tmp;
        for (auto op : *oper->Operands()) {
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
