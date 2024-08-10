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
 * File:   IntegrityChecker.cpp
 * Author: hs
 *
 * Created on August 10, 2024, 00:00 AM
 */

#include <Surelog/Common/FileSystem.h>
#include <Surelog/DesignCompile/IntegrityChecker.h>
#include <Surelog/ErrorReporting/ErrorContainer.h>
#include <Surelog/SourceCompile/SymbolTable.h>

// uhdm
#include <uhdm/array_var.h>
#include <uhdm/begin.h>
#include <uhdm/bit_select.h>
#include <uhdm/chandle_var.h>
#include <uhdm/checker_decl.h>
#include <uhdm/checker_inst.h>
#include <uhdm/class_defn.h>
#include <uhdm/class_obj.h>
#include <uhdm/concurrent_assertions.h>
#include <uhdm/design.h>
#include <uhdm/function.h>
#include <uhdm/indexed_part_select.h>
#include <uhdm/instance.h>
#include <uhdm/int_typespec.h>
#include <uhdm/interface_inst.h>
#include <uhdm/let_decl.h>
#include <uhdm/logic_var.h>
#include <uhdm/module_inst.h>
#include <uhdm/named_event.h>
#include <uhdm/named_event_array.h>
#include <uhdm/operation.h>
#include <uhdm/param_assign.h>
#include <uhdm/parameter.h>
#include <uhdm/part_select.h>
#include <uhdm/program.h>
#include <uhdm/property_decl.h>
#include <uhdm/range.h>
#include <uhdm/ref_module.h>
#include <uhdm/ref_obj.h>
#include <uhdm/ref_typespec.h>
#include <uhdm/sequence_decl.h>
#include <uhdm/typespec.h>
#include <uhdm/udp_defn.h>
#include <uhdm/variables.h>
#include <uhdm/virtual_interface_var.h>

namespace SURELOG {
IntegrityChecker::IntegrityChecker(FileSystem* fileSystem,
                                   SymbolTable* symbolTable,
                                   ErrorContainer* errorContainer)
    : m_fileSystem(fileSystem),
      m_symbolTable(symbolTable),
      m_errorContainer(errorContainer),
      m_acceptedObjectsWithInvalidLocations({
          UHDM::uhdmdesign,
          UHDM::uhdmsource_file,
          UHDM::uhdmpreproc_macro_definition,
          UHDM::uhdmpreproc_macro_instance,
      }) {}

bool IntegrityChecker::isBuiltPackageOnStack(
    const UHDM::any* const object) const {
  return ((object->UhdmType() == UHDM::uhdmpackage) &&
          (object->VpiName() == "builtin")) ||
         std::find_if(callstack.crbegin(), callstack.crend(),
                      [](const UHDM::any* const object) {
                        return (object->UhdmType() == UHDM::uhdmpackage) &&
                               (object->VpiName() == "builtin");
                      }) != callstack.rend();
}

template <typename T>
void IntegrityChecker::reportAmbigiousMembership(
    const std::vector<T*>* const collection, const T* const object) const {
  if (object == nullptr) return;
  if ((collection == nullptr) ||
      (std::find(collection->cbegin(), collection->cend(), object) ==
       collection->cend())) {
    Location loc(
        m_fileSystem->toPathId(object->VpiFile(), m_symbolTable),
        object->VpiLineNo(), object->VpiColumnNo(),
        m_symbolTable->registerSymbol(std::to_string(object->UhdmId())));
    Error err(ErrorDefinition::INTEGRITY_CHECK_OBJECT_NOT_IN_PARENT_COLLECTION,
              loc);
    m_errorContainer->addError(err);
  }
}

template <typename T>
void IntegrityChecker::reportDuplicates(const UHDM::any* const object,
                                        const std::vector<T*>* const collection,
                                        std::string_view name) const {
  if (collection == nullptr) return;
  const std::set<T*> unique(collection->cbegin(), collection->cend());
  if (unique.size() != collection->size()) {
    std::string text = std::to_string(object->UhdmId());
    text.append("::").append(name);
    Location loc(m_fileSystem->toPathId(object->VpiFile(), m_symbolTable),
                 object->VpiLineNo(), object->VpiColumnNo(),
                 m_symbolTable->registerSymbol(text));
    Error err(ErrorDefinition::INTEGRITY_CHECK_COLLECTION_HAS_DUPLICATES, loc);
    m_errorContainer->addError(err);
  }
}

void IntegrityChecker::reportInvalidLocation(
    const UHDM::any* const object) const {
  if (m_acceptedObjectsWithInvalidLocations.find(object->UhdmType()) !=
      m_acceptedObjectsWithInvalidLocations.cend())
    return;

  const UHDM::any* const parent = object->VpiParent();
  if (parent == nullptr) return;
  if (parent->UhdmType() == UHDM::uhdmdesign) return;

  // There are cases where things can be different files. e.g. PreprocTest
  if (object->VpiFile() != parent->VpiFile()) return;

  // Task body can be outside of the class definition itself!
  if ((object->UhdmType() == UHDM::uhdmtask) &&
      (parent->UhdmType() == UHDM::uhdmclass_defn))
    return;

  // Function body can be outside of the class definition itself!
  if ((object->UhdmType() == UHDM::uhdmfunction) &&
      (parent->UhdmType() == UHDM::uhdmclass_defn))
    return;

  // Function begin is implicit!
  if ((object->UhdmType() == UHDM::uhdmbegin) &&
      (parent->UhdmType() == UHDM::uhdmfunction))
    return;

  UHDM::UHDM_OBJECT_TYPE oType = object->UhdmType();
  if ((oType == UHDM::uhdmclass_typespec ||
       oType == UHDM::uhdmstruct_typespec) &&
      (object->VpiLineNo() == 0) && (object->VpiEndLineNo() == 0) &&
      (object->VpiColumnNo() == 0) && (object->VpiEndColumnNo() == 0))
    return;

  // Ports and Io_decl are declared in two different ways
  // Io_decl example - TaskDecls
  const std::map<UHDM::UHDM_OBJECT_TYPE, std::set<UHDM::UHDM_OBJECT_TYPE>>
      exclusions{{UHDM::uhdmref_typespec, {UHDM::uhdmport, UHDM::uhdmio_decl}},
                 {UHDM::uhdmconstant, {UHDM::uhdmport}}};

  if (auto it1 = exclusions.find(object->UhdmType());
      it1 != exclusions.cend()) {
    if (auto it2 = it1->second.find(parent->UhdmType());
        it2 != it1->second.cend())
      return;
  }

  const uint32_t childLineNo = object->VpiLineNo();
  const uint32_t childColumnNo = object->VpiColumnNo();
  const uint32_t childEndLineNo = object->VpiEndLineNo();
  const uint32_t childEndColumnNo = object->VpiEndColumnNo();

  const uint32_t parentLineNo = parent->VpiLineNo();
  const uint32_t parentColumnNo = parent->VpiColumnNo();
  const uint32_t parentEndLineNo = parent->VpiEndLineNo();
  const uint32_t parentEndColumnNo = parent->VpiEndColumnNo();

  bool valid = childEndLineNo <= parentEndLineNo;
  if (any_cast<UHDM::ref_typespec>(object) != nullptr) {
    valid = valid && (childLineNo <= parentLineNo);

    // type i.e. child should be on the left (or overlapping) of the parent
    if (childLineNo == parentEndLineNo) {
      valid = valid && (childEndColumnNo <= parentEndColumnNo);
    }
  } else {
    valid = valid && (childLineNo >= parentLineNo);

    if (childLineNo == parentLineNo) {
      valid = valid && (childColumnNo >= parentColumnNo);
    }

    if (childEndLineNo == parentEndLineNo) {
      valid = valid && (childEndColumnNo <= parentEndColumnNo);
    }
  }

  if (!valid) {
    Location loc(
        m_fileSystem->toPathId(object->VpiFile(), m_symbolTable),
        object->VpiLineNo(), object->VpiColumnNo(),
        m_symbolTable->registerSymbol(std::to_string(object->UhdmId())));
    Error err(
        ErrorDefinition::INTEGRITY_CHECK_CHILD_NOT_ENTIRELY_IN_PARENT_BOUNDARY,
        loc);
    m_errorContainer->addError(err);
  }
}

void IntegrityChecker::reportMissingLocation(
    const UHDM::any* const object) const {
  if ((object->VpiLineNo() != 0) && (object->VpiColumnNo() != 0) &&
      (object->VpiEndLineNo() != 0) && (object->VpiEndColumnNo() != 0))
    return;

  if (m_acceptedObjectsWithInvalidLocations.find(object->UhdmType()) !=
      m_acceptedObjectsWithInvalidLocations.cend())
    return;

  const UHDM::any* const parent = object->VpiParent();
  const UHDM::any* const grandParent =
      (parent == nullptr) ? parent : parent->VpiParent();

  // begin in function body are implicit!
  if ((object->UhdmType() == UHDM::uhdmbegin) && (parent != nullptr) &&
      (parent->UhdmType() == UHDM::uhdmfunction))
    return;

  if ((object->UhdmType() == UHDM::uhdmref_typespec) && (parent != nullptr) &&
      (grandParent != nullptr) && (grandParent->VpiName() == "new") &&
      (parent->Cast<UHDM::variables>() != nullptr) &&
      (grandParent->UhdmType() == UHDM::uhdmfunction)) {
    // For ref_typespec associated with a class's constructor return value
    // there is no legal position because the "new" operator's return value
    // is implicit.
    const UHDM::variables* const parentAsVariables =
        parent->Cast<UHDM::variables>();
    const UHDM::function* const grandParentAsFunction =
        grandParent->Cast<UHDM::function>();
    if ((grandParentAsFunction->Return() == parent) &&
        (parentAsVariables->Typespec() == object)) {
      return;
    }
  } else if ((object->UhdmType() == UHDM::uhdmclass_typespec) &&
             (parent != nullptr) && (parent->VpiName() == "new") &&
             (parent->UhdmType() == UHDM::uhdmfunction)) {
    // For typespec associated with a class's constructor return value
    // there is no legal position because the "new" operator's return value
    // is implicit.
    const UHDM::function* const parentAsFunction =
        parent->Cast<UHDM::function>();
    if (const UHDM::variables* const var = parentAsFunction->Return()) {
      if (const UHDM::ref_typespec* const rt = var->Typespec()) {
        if ((rt == object) || (rt->Actual_typespec() == object)) {
          return;
        }
      }
    }
  } else if ((object->Cast<UHDM::variables>() != nullptr) &&
             (parent != nullptr) &&
             (parent->UhdmType() == UHDM::uhdmfunction)) {
    // When no explicit return is specific, the function's name
    // is consdiered the return type's name.
    const UHDM::function* const parentAsFunction =
        parent->Cast<UHDM::function>();
    if (parentAsFunction->Return() == object) return;
  } else if ((object->UhdmType() == UHDM::uhdmconstant) &&
             (parent != nullptr) && (parent->UhdmType() == UHDM::uhdmrange)) {
    // The left expression of range is allowed to be zero.
    const UHDM::range* const parentAsRange = parent->Cast<UHDM::range>();
    if (parentAsRange->Left_expr() == object) return;
  }

  std::string text = std::to_string(object->UhdmId());
  text.append(", type: ").append(UHDM::UhdmName(object->UhdmType()));

  Location loc(m_fileSystem->toPathId(object->VpiFile(), m_symbolTable),
               object->VpiLineNo(), object->VpiColumnNo(),
               m_symbolTable->registerSymbol(text));
  Error err(ErrorDefinition::INTEGRITY_CHECK_MISSING_LOCATION, loc);
  m_errorContainer->addError(err);
}

bool IntegrityChecker::isImplicitFunctionReturnType(
    const UHDM::any* const object) {
  if (const UHDM::variables* v = any_cast<UHDM::variables>(object)) {
    if (const UHDM::function* f =
            any_cast<UHDM::function>(object->VpiParent())) {
      if ((f->Return() == v) && v->VpiName().empty()) return true;
    }
  }
  return false;
}

std::string_view IntegrityChecker::stripDecorations(std::string_view name) {
  while (!name.empty() && name.back() == ':') name.remove_suffix(1);

  size_t pos1 = name.rfind("::");
  if (pos1 != std::string::npos) name = name.substr(pos1 + 2);

  size_t pos2 = name.rfind('.');
  if (pos2 != std::string::npos) name = name.substr(pos2 + 1);

  size_t pos3 = name.rfind('@');
  if (pos3 != std::string::npos) name = name.substr(pos3 + 1);

  return name;
}

bool IntegrityChecker::areNamedSame(const UHDM::any* const object,
                                    const UHDM::any* const actual) {
  std::string_view objectName = stripDecorations(object->VpiName());
  std::string_view actualName = stripDecorations(actual->VpiName());
  return (objectName == actualName);
}

void IntegrityChecker::reportInvalidNames(const UHDM::any* const object) const {
  // Function implicit return type are unnammed.
  if (isImplicitFunctionReturnType(object)) return;

  bool shouldReport = false;

  if (object->UhdmType() == UHDM::uhdmref_obj) {
    shouldReport = (object->VpiName() == SymbolTable::getBadSymbol());
    shouldReport = shouldReport || object->VpiName().empty();

    if (const UHDM::any* const actual =
            static_cast<const UHDM::ref_obj*>(object)->Actual_group()) {
      shouldReport = shouldReport || !areNamedSame(object, actual);
      shouldReport = shouldReport && !isImplicitFunctionReturnType(actual);
      shouldReport = shouldReport && (object->VpiName() != "super");
      shouldReport = shouldReport && (object->VpiName() != "this");
    }
  } else if (object->UhdmType() == UHDM::uhdmref_typespec) {
    shouldReport = (object->VpiName() == SymbolTable::getBadSymbol());
    if (const UHDM::any* actual =
            static_cast<const UHDM::ref_typespec*>(object)->Actual_typespec()) {
      if ((actual->UhdmType() == UHDM::uhdmstruct_typespec) ||
          (actual->UhdmType() == UHDM::uhdmunion_typespec) ||
          (actual->UhdmType() == UHDM::uhdmenum_typespec)) {
        //@todo: Need to impliment typedefAlias "test/PreprocUhdmCov"
        shouldReport = false;
      } else if ((actual->UhdmType() == UHDM::uhdmclass_typespec) ||
                 (actual->UhdmType() == UHDM::uhdmmodule_typespec) ||
                 (actual->UhdmType() == UHDM::uhdmenum_typespec) ||
                 (actual->UhdmType() == UHDM::uhdminterface_typespec)) {
        shouldReport = shouldReport || object->VpiName().empty();
        shouldReport = shouldReport || !areNamedSame(object, actual);
      }
    } else {
      shouldReport = shouldReport || object->VpiName().empty();
    }
  }

  if (shouldReport) {
    Location loc(
        m_fileSystem->toPathId(object->VpiFile(), m_symbolTable),
        object->VpiLineNo(), object->VpiColumnNo(),
        m_symbolTable->registerSymbol(std::to_string(object->UhdmId())));
    Error err(ErrorDefinition::INTEGRITY_CHECK_MISSING_NAME, loc);
    m_errorContainer->addError(err);
  }
}

void IntegrityChecker::reportInvalidFile(const UHDM::any* const object) const {
  std::string_view filename = object->VpiFile();
  if (filename.empty() || (filename == SymbolTable::getBadSymbol())) {
    Location loc(
        m_fileSystem->toPathId(object->VpiFile(), m_symbolTable),
        object->VpiLineNo(), object->VpiColumnNo(),
        m_symbolTable->registerSymbol(std::to_string(object->UhdmId())));
    Error err(ErrorDefinition::INTEGRITY_CHECK_MISSING_FILE, loc);
    m_errorContainer->addError(err);
  }
}

void IntegrityChecker::reportNullActual(const UHDM::any* const object) const {
  if (isBuiltPackageOnStack(object)) return;

  bool shouldReport = false;
  switch (object->UhdmType()) {
    case UHDM::uhdmref_obj: {
      shouldReport =
          static_cast<const UHDM::ref_obj*>(object)->Actual_group() == nullptr;
      // Special case for $root and few others
      shouldReport =
          shouldReport &&
          !(((object->VpiName() == "$root") || (object->VpiName() == "size") ||
             (object->VpiName() == "delete")) &&
            (object->VpiParent() != nullptr) &&
            (object->VpiParent()->UhdmType() == UHDM::uhdmhier_path));
    } break;

    case UHDM::uhdmref_typespec: {
      shouldReport =
          static_cast<const UHDM::ref_typespec*>(object)->Actual_typespec() ==
          nullptr;
    } break;

    case UHDM::uhdmbit_select: {
      shouldReport =
          static_cast<const UHDM::bit_select*>(object)->Actual_group() ==
          nullptr;
    } break;

    case UHDM::uhdmpart_select: {
      shouldReport =
          static_cast<const UHDM::part_select*>(object)->Actual_group() ==
          nullptr;
    } break;

    case UHDM::uhdmindexed_part_select: {
      shouldReport = static_cast<const UHDM::indexed_part_select*>(object)
                         ->Actual_group() == nullptr;
    } break;

    case UHDM::uhdmref_module: {
      shouldReport =
          static_cast<const UHDM::ref_module*>(object)->Actual_group() ==
          nullptr;
    } break;

    case UHDM::uhdmchandle_var: {
      shouldReport =
          static_cast<const UHDM::chandle_var*>(object)->Actual_group() ==
          nullptr;
    } break;

    default:
      break;
  }

  if (shouldReport) {
    Location loc(
        m_fileSystem->toPathId(object->VpiFile(), m_symbolTable),
        object->VpiLineNo(), object->VpiColumnNo(),
        m_symbolTable->registerSymbol(std::to_string(object->UhdmId())));
    Error err(ErrorDefinition::INTEGRITY_CHECK_ACTUAL_CANNOT_BE_NULL, loc);
    m_errorContainer->addError(err);
  }
}

void IntegrityChecker::enterAny(const UHDM::any* const object) {
  if (isBuiltPackageOnStack(object)) return;

  reportNullActual(object);

  const UHDM::scope* const objectAsScope = any_cast<UHDM::scope>(object);
  const UHDM::design* const objectAsDesign = any_cast<UHDM::design>(object);

  if (objectAsDesign != nullptr) {
    reportDuplicates(object, objectAsDesign->Typespecs(), "Typespecs");
    reportDuplicates(object, objectAsDesign->Let_decls(), "Let_decls");
    reportDuplicates(object, objectAsDesign->Task_funcs(), "Task_funcs");
    reportDuplicates(object, objectAsDesign->Parameters(), "Parameters");
    reportDuplicates(object, objectAsDesign->Param_assigns(), "Param_assigns");
    reportDuplicates(object, objectAsDesign->AllPackages(), "AllPackages");
    reportDuplicates(object, objectAsDesign->AllClasses(), "AllClasses");
    reportDuplicates(object, objectAsDesign->AllInterfaces(), "AllInterfaces");
    reportDuplicates(object, objectAsDesign->AllUdps(), "AllUdps");
    reportDuplicates(object, objectAsDesign->AllPrograms(), "AllPrograms");
    reportDuplicates(object, objectAsDesign->AllModules(), "AllModules");
    return;
  }
  if (objectAsScope != nullptr) {
    reportDuplicates(object, objectAsScope->Array_vars(), "Array_vars");
    reportDuplicates(object, objectAsScope->Concurrent_assertions(),
                     "Concurrent_assertions");
    reportDuplicates(object, objectAsScope->Instance_items(), "Instance_items");
    reportDuplicates(object, objectAsScope->Let_decls(), "Let_decls");
    reportDuplicates(object, objectAsScope->Logic_vars(), "Logic_vars");
    reportDuplicates(object, objectAsScope->Named_event_arrays(),
                     "Named_event_arrays");
    reportDuplicates(object, objectAsScope->Named_events(), "Named_events");
    reportDuplicates(object, objectAsScope->Param_assigns(), "Param_assigns");
    reportDuplicates(object, objectAsScope->Parameters(), "Parameters");
    reportDuplicates(object, objectAsScope->Property_decls(), "Property_decls");
    reportDuplicates(object, objectAsScope->Scopes(), "Scopes");
    reportDuplicates(object, objectAsScope->Sequence_decls(), "Sequence_decls");
    reportDuplicates(object, objectAsScope->Typespecs(), "Typespecs");
    reportDuplicates(object, objectAsScope->Variables(), "Variables");
    reportDuplicates(object, objectAsScope->Virtual_interface_vars(),
                     "Virtual_interface_vars");
  }
  if (const UHDM::udp_defn* const objectAsUdpDefn =
          any_cast<UHDM::udp_defn>(object)) {
    reportDuplicates(object, objectAsUdpDefn->Io_decls(), "Io_decls");
    reportDuplicates(object, objectAsUdpDefn->Table_entrys(), "Table_entrys");
  }
  if (const UHDM::class_defn* const objectAsClassDefn =
          any_cast<UHDM::class_defn>(object)) {
    reportDuplicates(object, objectAsClassDefn->Deriveds(), "Deriveds");
    reportDuplicates(object, objectAsClassDefn->Class_typespecs(),
                     "Class_typespecs");
    reportDuplicates(object, objectAsClassDefn->Constraints(), "Constraints");
    reportDuplicates(object, objectAsClassDefn->Task_funcs(), "Task_funcs");
  }
  if (const UHDM::class_obj* const objectAsClassObj =
          any_cast<UHDM::class_obj>(object)) {
    reportDuplicates(object, objectAsClassObj->Constraints(), "Constraints");
    reportDuplicates(object, objectAsClassObj->Messages(), "Messages");
    reportDuplicates(object, objectAsClassObj->Task_funcs(), "Task_funcs");
    reportDuplicates(object, objectAsClassObj->Threads(), "Threads");
  }
  if (const UHDM::instance* const objectAsInstance =
          any_cast<UHDM::instance>(object)) {
    reportDuplicates(object, objectAsInstance->Array_nets(), "Array_nets");
    reportDuplicates(object, objectAsInstance->Assertions(), "Assertions");
    reportDuplicates(object, objectAsInstance->Class_defns(), "Class_defns");
    reportDuplicates(object, objectAsInstance->Nets(), "Nets");
    reportDuplicates(object, objectAsInstance->Programs(), "Programs");
    reportDuplicates(object, objectAsInstance->Program_arrays(),
                     "Program_arrays");
    reportDuplicates(object, objectAsInstance->Spec_params(), "Spec_params");
    reportDuplicates(object, objectAsInstance->Task_funcs(), "Task_funcs");
  }
  if (const UHDM::checker_decl* const objectAsCheckerDecl =
          any_cast<UHDM::checker_decl>(object)) {
    reportDuplicates(object, objectAsCheckerDecl->Ports(), "Ports");
    reportDuplicates(object, objectAsCheckerDecl->Cont_assigns(),
                     "Cont_assigns");
    reportDuplicates(object, objectAsCheckerDecl->Process(), "Process");
  }
  if (const UHDM::checker_inst* const objectAsCheckerInst =
          any_cast<UHDM::checker_inst>(object)) {
    reportDuplicates(object, objectAsCheckerInst->Ports(), "Ports");
  }
  if (const UHDM::interface_inst* const objectAsInterfaceInst =
          any_cast<UHDM::interface_inst>(object)) {
    reportDuplicates(object, objectAsInterfaceInst->Clocking_blocks(),
                     "Io_dClocking_blocksecls");
    reportDuplicates(object, objectAsInterfaceInst->Cont_assigns(),
                     "Cont_assigns");
    reportDuplicates(object, objectAsInterfaceInst->Gen_scope_arrays(),
                     "Gen_scope_arrays");
    reportDuplicates(object, objectAsInterfaceInst->Gen_stmts(), "Gen_stmts");
    reportDuplicates(object, objectAsInterfaceInst->Interface_arrays(),
                     "Interface_arrays");
    reportDuplicates(object, objectAsInterfaceInst->Interfaces(), "Interfaces");
    reportDuplicates(object, objectAsInterfaceInst->Interface_tf_decls(),
                     "Interface_tf_decls");
    reportDuplicates(object, objectAsInterfaceInst->Mod_paths(), "Mod_paths");
    reportDuplicates(object, objectAsInterfaceInst->Modports(), "Modports");
    reportDuplicates(object, objectAsInterfaceInst->Ports(), "Ports");
    reportDuplicates(object, objectAsInterfaceInst->Process(), "Process");
    reportDuplicates(object, objectAsInterfaceInst->Elab_tasks(), "Elab_tasks");
  }
  if (const UHDM::module_inst* const objectAsModuleInst =
          any_cast<UHDM::module_inst>(object)) {
    reportDuplicates(object, objectAsModuleInst->Alias_stmts(), "Alias_stmts");
    reportDuplicates(object, objectAsModuleInst->Clocking_blocks(),
                     "Clocking_blocks");
    reportDuplicates(object, objectAsModuleInst->Cont_assigns(),
                     "Cont_assigns");
    reportDuplicates(object, objectAsModuleInst->Def_params(), "Def_params");
    reportDuplicates(object, objectAsModuleInst->Gen_scope_arrays(),
                     "Gen_scope_arrays");
    reportDuplicates(object, objectAsModuleInst->Gen_stmts(), "Gen_stmts");
    reportDuplicates(object, objectAsModuleInst->Interface_arrays(),
                     "Interface_arrays");
    reportDuplicates(object, objectAsModuleInst->Interfaces(), "Interfaces");
    reportDuplicates(object, objectAsModuleInst->Io_decls(), "Io_decls");
    reportDuplicates(object, objectAsModuleInst->Mod_paths(), "Mod_paths");
    reportDuplicates(object, objectAsModuleInst->Module_arrays(),
                     "Module_arrays");
    reportDuplicates(object, objectAsModuleInst->Modules(), "Modules");
    reportDuplicates(object, objectAsModuleInst->Ports(), "Ports");
    reportDuplicates(object, objectAsModuleInst->Primitives(), "Primitives");
    reportDuplicates(object, objectAsModuleInst->Primitive_arrays(),
                     "Primitive_arrays");
    reportDuplicates(object, objectAsModuleInst->Process(), "Process");
    reportDuplicates(object, objectAsModuleInst->Ref_modules(), "Ref_modules");
    reportDuplicates(object, objectAsModuleInst->Tchks(), "Tchks");
    reportDuplicates(object, objectAsModuleInst->Elab_tasks(), "Elab_tasks");
  }
  if (const UHDM::program* const objectAsProgram =
          any_cast<UHDM::program>(object)) {
    reportDuplicates(object, objectAsProgram->Clocking_blocks(),
                     "Clocking_blocks");
    reportDuplicates(object, objectAsProgram->Cont_assigns(), "Cont_assigns");
    reportDuplicates(object, objectAsProgram->Gen_scope_arrays(),
                     "Gen_scope_arrays");
    reportDuplicates(object, objectAsProgram->Interface_arrays(),
                     "Interface_arrays");
    reportDuplicates(object, objectAsProgram->Interfaces(), "Interfaces");
    reportDuplicates(object, objectAsProgram->Ports(), "Ports");
    reportDuplicates(object, objectAsProgram->Process(), "Process");
  }

  // Known Issues!
  if (const UHDM::int_typespec* const t =
          any_cast<UHDM::int_typespec>(object)) {
    if (const UHDM::expr* const e = t->Cast_to_expr()) {
      visited.emplace(e);
    }
  } else if (const UHDM::operation* const op =
                 any_cast<UHDM::operation>(object)) {
    if (op->VpiOpType() == vpiCastOp) {
      if (const UHDM::ref_typespec* const rt = op->Typespec()) {
        if (const UHDM::int_typespec* const t =
                rt->Actual_typespec<UHDM::int_typespec>()) {
          if (const UHDM::expr* const e = t->Cast_to_expr()) {
            visited.emplace(e);
          }
        }
      }
    }
  }

  reportMissingLocation(object);
  reportInvalidNames(object);
  reportInvalidFile(object);

  const UHDM::any* const parent = object->VpiParent();
  if (parent == nullptr) {
    Location loc(
        m_fileSystem->toPathId(object->VpiFile(), m_symbolTable),
        object->VpiLineNo(), object->VpiColumnNo(),
        m_symbolTable->registerSymbol(std::to_string(object->UhdmId())));
    Error err(ErrorDefinition::INTEGRITY_CHECK_MISSING_PARENT, loc);
    m_errorContainer->addError(err);
    return;
  }

  reportInvalidLocation(object);

  const UHDM::scope* const parentAsScope = any_cast<UHDM::scope>(parent);
  const UHDM::design* const parentAsDesign = any_cast<UHDM::design>(parent);
  const UHDM::udp_defn* const parentAsUdpDefn =
      any_cast<UHDM::udp_defn>(parent);

  const std::set<UHDM::UHDM_OBJECT_TYPE> allowedScopeChildren{
      UHDM::uhdmarray_net,
      UHDM::uhdmarray_typespec,
      UHDM::uhdmarray_var,
      UHDM::uhdmassert_stmt,
      UHDM::uhdmassume,
      UHDM::uhdmbit_typespec,
      UHDM::uhdmbit_var,
      UHDM::uhdmbyte_typespec,
      UHDM::uhdmbyte_var,
      UHDM::uhdmchandle_typespec,
      UHDM::uhdmchandle_var,
      UHDM::uhdmclass_typespec,
      UHDM::uhdmclass_var,
      UHDM::uhdmconcurrent_assertions,
      UHDM::uhdmcover,
      UHDM::uhdmenum_net,
      UHDM::uhdmenum_typespec,
      UHDM::uhdmenum_var,
      UHDM::uhdmevent_typespec,
      UHDM::uhdmimport_typespec,
      UHDM::uhdmint_typespec,
      UHDM::uhdmint_var,
      UHDM::uhdminteger_net,
      UHDM::uhdminteger_typespec,
      UHDM::uhdminteger_var,
      UHDM::uhdminterface_typespec,
      UHDM::uhdmlet_decl,
      UHDM::uhdmlogic_net,
      UHDM::uhdmlogic_typespec,
      UHDM::uhdmlogic_var,
      UHDM::uhdmlong_int_typespec,
      UHDM::uhdmlong_int_var,
      UHDM::uhdmmodule_typespec,
      UHDM::uhdmnamed_event,
      UHDM::uhdmnamed_event_array,
      UHDM::uhdmnet_bit,
      UHDM::uhdmpacked_array_net,
      UHDM::uhdmpacked_array_typespec,
      UHDM::uhdmpacked_array_var,
      UHDM::uhdmparam_assign,
      UHDM::uhdmparameter,
      UHDM::uhdmproperty_decl,
      UHDM::uhdmproperty_typespec,
      UHDM::uhdmreal_typespec,
      UHDM::uhdmreal_var,
      UHDM::uhdmref_var,
      UHDM::uhdmrestrict,
      UHDM::uhdmsequence_decl,
      UHDM::uhdmsequence_typespec,
      UHDM::uhdmshort_int_typespec,
      UHDM::uhdmshort_int_var,
      UHDM::uhdmshort_real_typespec,
      UHDM::uhdmshort_real_var,
      UHDM::uhdmstring_typespec,
      UHDM::uhdmstring_var,
      UHDM::uhdmstruct_net,
      UHDM::uhdmstruct_typespec,
      UHDM::uhdmstruct_var,
      UHDM::uhdmtime_net,
      UHDM::uhdmtime_typespec,
      UHDM::uhdmtime_var,
      UHDM::uhdmtype_parameter,
      UHDM::uhdmunion_typespec,
      UHDM::uhdmunion_var,
      UHDM::uhdmunsupported_typespec,
      UHDM::uhdmvar_bit,
      UHDM::uhdmvirtual_interface_var,
      UHDM::uhdmvoid_typespec,
  };

  bool expectScope = (allowedScopeChildren.find(object->UhdmType()) !=
                      allowedScopeChildren.cend());
  if (any_cast<UHDM::begin>(object) != nullptr) {
    expectScope = false;
  }

  const std::set<UHDM::UHDM_OBJECT_TYPE> allowedDesignChildren{
      UHDM::uhdmpackage,  UHDM::uhdmmodule_inst, UHDM::uhdmclass_defn,
      UHDM::uhdmtypespec, UHDM::uhdmlet_decl,    UHDM::uhdmfunction,
      UHDM::uhdmtask,     UHDM::uhdmparameter,   UHDM::uhdmparam_assign};
  bool expectDesign = (allowedDesignChildren.find(object->UhdmType()) !=
                       allowedDesignChildren.cend());

  const std::set<UHDM::UHDM_OBJECT_TYPE> allowedUdpChildren{
      UHDM::uhdmlogic_net, UHDM::uhdmio_decl, UHDM::uhdmtable_entry};
  bool expectUdpDefn = (allowedUdpChildren.find(object->UhdmType()) !=
                        allowedUdpChildren.cend());

  if ((parentAsScope == nullptr) && (parentAsDesign == nullptr) &&
      (parentAsUdpDefn == nullptr) &&
      (expectScope || expectDesign || expectUdpDefn)) {
    Location loc(
        m_fileSystem->toPathId(object->VpiFile(), m_symbolTable),
        object->VpiLineNo(), object->VpiColumnNo(),
        m_symbolTable->registerSymbol(std::to_string(object->UhdmId())));
    Error err(
        ErrorDefinition::INTEGRITY_CHECK_PARENT_IS_NEITHER_SCOPE_NOR_DESIGN,
        loc);
    m_errorContainer->addError(err);
  }

  if (parentAsDesign != nullptr) {
    reportAmbigiousMembership(parentAsDesign->Typespecs(),
                              any_cast<UHDM::typespec>(object));
    reportAmbigiousMembership(parentAsDesign->Let_decls(),
                              any_cast<UHDM::let_decl>(object));
    reportAmbigiousMembership(parentAsDesign->Task_funcs(),
                              any_cast<UHDM::task_func>(object));
    reportAmbigiousMembership(
        parentAsDesign->Parameters(),
        (any_cast<UHDM::parameter>(object) != nullptr) ? object : nullptr);
    reportAmbigiousMembership(parentAsDesign->Param_assigns(),
                              any_cast<UHDM::param_assign>(object));
  } else if (parentAsScope != nullptr) {
    reportAmbigiousMembership(
        parentAsScope->Parameters(),
        (any_cast<UHDM::parameter>(object) != nullptr) ? object : nullptr);
    reportAmbigiousMembership(parentAsScope->Param_assigns(),
                              any_cast<UHDM::param_assign>(object));
    reportAmbigiousMembership(parentAsScope->Typespecs(),
                              any_cast<UHDM::typespec>(object));
    reportAmbigiousMembership(parentAsScope->Variables(),
                              any_cast<UHDM::variables>(object));
    reportAmbigiousMembership(parentAsScope->Property_decls(),
                              any_cast<UHDM::property_decl>(object));
    reportAmbigiousMembership(parentAsScope->Sequence_decls(),
                              any_cast<UHDM::sequence_decl>(object));
    reportAmbigiousMembership(parentAsScope->Concurrent_assertions(),
                              any_cast<UHDM::concurrent_assertions>(object));
    reportAmbigiousMembership(parentAsScope->Named_events(),
                              any_cast<UHDM::named_event>(object));
    reportAmbigiousMembership(parentAsScope->Named_event_arrays(),
                              any_cast<UHDM::named_event_array>(object));
    reportAmbigiousMembership(parentAsScope->Virtual_interface_vars(),
                              any_cast<UHDM::virtual_interface_var>(object));
    reportAmbigiousMembership(parentAsScope->Logic_vars(),
                              any_cast<UHDM::logic_var>(object));
    reportAmbigiousMembership(parentAsScope->Array_vars(),
                              any_cast<UHDM::array_var>(object));
    reportAmbigiousMembership(parentAsScope->Let_decls(),
                              any_cast<UHDM::let_decl>(object));
    reportAmbigiousMembership(parentAsScope->Scopes(),
                              any_cast<UHDM::scope>(object));
  }
}

void IntegrityChecker::check(const UHDM::design* const object) {
  listenAny(object);
}

void IntegrityChecker::check(const std::vector<const UHDM::design*>& objects) {
  for (const UHDM::design* d : objects) {
    check(d);
  }
}
}  // namespace SURELOG
