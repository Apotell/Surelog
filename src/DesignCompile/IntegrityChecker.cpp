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
#include <Surelog/Common/Session.h>
#include <Surelog/DesignCompile/IntegrityChecker.h>
#include <Surelog/ErrorReporting/ErrorContainer.h>
#include <Surelog/SourceCompile/SymbolTable.h>
#include <Surelog/Utils/StringUtils.h>

// uhdm
#include <uhdm/uhdm.h>

namespace SURELOG {
IntegrityChecker::IntegrityChecker(Session* session)
    : m_session(session),
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

bool IntegrityChecker::isUVMMember(const UHDM::any* const object) const {
  std::string_view filepath = object->VpiFile();
  return (filepath.find("\\uvm_") != std::string_view::npos) ||
         (filepath.find("/uvm_") != std::string_view::npos) ||
         (filepath.find("\\ovm_") != std::string_view::npos) ||
         (filepath.find("/ovm_") != std::string_view::npos);
}

template <typename T>
void IntegrityChecker::reportAmbigiousMembership(
    const std::vector<T*>* const collection, const T* const object) const {
  if (object == nullptr) return;
  if ((collection == nullptr) ||
      (std::find(collection->cbegin(), collection->cend(), object) ==
       collection->cend())) {
    SymbolTable* const symbolTable = m_session->getSymbolTable();
    FileSystem* const fileSystem = m_session->getFileSystem();
    ErrorContainer* const errorContainer = m_session->getErrorContainer();

    Location loc(fileSystem->toPathId(object->VpiFile(), symbolTable),
                 object->VpiLineNo(), object->VpiColumnNo(),
                 symbolTable->registerSymbol(std::to_string(object->UhdmId())));
    errorContainer->addError(
        ErrorDefinition::INTEGRITY_CHECK_OBJECT_NOT_IN_PARENT_COLLECTION, loc);
  }
}

template <typename T>
void IntegrityChecker::reportDuplicates(const UHDM::any* const object,
                                        const std::vector<T*>* const collection,
                                        std::string_view name) const {
  if (collection == nullptr) return;
  if (isUVMMember(object)) return;

  const std::set<T*> unique(collection->cbegin(), collection->cend());
  if (unique.size() != collection->size()) {
    SymbolTable* const symbolTable = m_session->getSymbolTable();
    FileSystem* const fileSystem = m_session->getFileSystem();
    ErrorContainer* const errorContainer = m_session->getErrorContainer();

    std::string text = std::to_string(object->UhdmId());
    text.append("::").append(name);
    Location loc(fileSystem->toPathId(object->VpiFile(), symbolTable),
                 object->VpiLineNo(), object->VpiColumnNo(),
                 symbolTable->registerSymbol(text));
    errorContainer->addError(
        ErrorDefinition::INTEGRITY_CHECK_COLLECTION_HAS_DUPLICATES, loc);
  }
}

inline IntegrityChecker::LineColumnRelation
IntegrityChecker::getLineColumnRelation(uint32_t csl, uint16_t csc,
                                        uint32_t cel, uint16_t cec) const {
  if (csl == cel) {
    if (csc < cec) return LineColumnRelation::Before;
    if (csc == cec) return LineColumnRelation::Inside;
    if (csc > cec) return LineColumnRelation::After;
  }

  return (csl < cel) ? LineColumnRelation::Before : LineColumnRelation::After;
}

inline IntegrityChecker::LineColumnRelation
IntegrityChecker::getLineColumnRelation(uint32_t csl, uint16_t csc,
                                        uint32_t cel, uint16_t cec,
                                        uint32_t psl, uint16_t psc,
                                        uint32_t pel, uint16_t pec) const {
  if (cel < psl) return LineColumnRelation::Before;
  if (csl > pel) return LineColumnRelation::After;

  if ((csl == pel) && (csc >= pec)) return LineColumnRelation::After;
  if ((cel == psl) && (cec <= psc)) return LineColumnRelation::Before;

  const bool startIsInside = (csl > psl) || ((csl == psl) && (csc >= psc));
  const bool endIsInside = (cel < pel) || ((cel == pel) && (cec <= pec));
  if (startIsInside && endIsInside) return LineColumnRelation::Inside;

  return LineColumnRelation::Inconclusive;
}

void IntegrityChecker::reportInvalidLocation(
    const UHDM::any* const object) const {
  if ((object->VpiLineNo() == 0) && (object->VpiEndLineNo() == 0) &&
      (object->VpiColumnNo() == 0) && (object->VpiEndColumnNo() == 0))
    return;

  const UHDM::any* const parent = object->VpiParent();
  if (parent == nullptr) return;
  if (parent->UhdmType() == UHDM::uhdmdesign) return;

  if ((parent->VpiLineNo() == 0) && (parent->VpiEndLineNo() == 0) &&
      (parent->VpiColumnNo() == 0) && (parent->VpiEndColumnNo() == 0))
    return;

  if (m_acceptedObjectsWithInvalidLocations.find(object->UhdmType()) !=
      m_acceptedObjectsWithInvalidLocations.cend())
    return;

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

  // REVISIT(HS): Temporarily ignore hier_path issues
  if ((object->UhdmType() == UHDM::uhdmhier_path) ||
      (parent->UhdmType() == UHDM::uhdmhier_path))
    return;

  const uint32_t csl = object->VpiLineNo();
  const uint32_t csc = object->VpiColumnNo();
  const uint32_t cel = object->VpiEndLineNo();
  const uint32_t cec = object->VpiEndColumnNo();

  SymbolTable* const symbolTable = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  ErrorContainer* const errorContainer = m_session->getErrorContainer();

  LineColumnRelation actualRelation = getLineColumnRelation(csl, csc, cel, cec);
  if ((actualRelation != LineColumnRelation::Before) &&
      (actualRelation != LineColumnRelation::Inside)) {
    Location loc(
        fileSystem->toPathId(object->VpiFile(), symbolTable),
        object->VpiLineNo(), object->VpiColumnNo(),
        symbolTable->registerSymbol(StrCat("Object: ", object->UhdmId())));
    errorContainer->addError(ErrorDefinition::INTEGRITY_CHECK_INVALID_LOCATION,
                             loc);
    return;
  }

  const uint32_t psl = parent->VpiLineNo();
  const uint32_t psc = parent->VpiColumnNo();
  const uint32_t pel = parent->VpiEndLineNo();
  const uint32_t pec = parent->VpiEndColumnNo();

  actualRelation = getLineColumnRelation(psl, psc, pel, pec);
  if ((actualRelation != LineColumnRelation::Before) &&
      (actualRelation != LineColumnRelation::Inside))
    // If parent location is known to be bad, don't bother reporting issues
    // with the child. Parent is already reported and so when the parent
    // gets fixed, the child becomes important.
    return;

  actualRelation =
      getLineColumnRelation(csl, csc, cel, cec, psl, psc, pel, pec);

  LineColumnRelation expectedRelation = LineColumnRelation::Inside;
  if (const UHDM::ref_typespec* const objectAsRef_typespec =
          object->Cast<UHDM::ref_typespec>()) {
    if (objectAsRef_typespec->Actual_typespec<UHDM::unsupported_typespec>() !=
        nullptr) {
      // Ignore issues with unsupported_typespec.
      // There are known issues with genvar not followed with a type.
      return;
    }

    if ((parent->Cast<UHDM::extends>() != nullptr) ||
        (parent->Cast<UHDM::tf_call>() != nullptr)) {
      expectedRelation = LineColumnRelation::Inside;
    } else if (parent->Cast<UHDM::type_parameter>() != nullptr) {
      expectedRelation = LineColumnRelation::After;
    } else {
      expectedRelation = LineColumnRelation::Before;
    }

    if (const UHDM::enum_typespec* const parentAsEnum_typespec =
            parent->Cast<UHDM::enum_typespec>()) {
      if (parentAsEnum_typespec->Base_typespec() == object) {
        // typedef enum <base_type> { ... }
        expectedRelation = LineColumnRelation::Inside;
      }
    } else if (const UHDM::tagged_pattern* const parentAsTagged_pattern =
                   parent->Cast<UHDM::tagged_pattern>()) {
      if (parentAsTagged_pattern->Typespec() == object) {
        // BlahBlab: constant|operation
        expectedRelation = LineColumnRelation::Inside;
      }
    } else if (const UHDM::packed_array_typespec* const
                   parentAsPacked_array_typespec =
                       parent->Cast<UHDM::packed_array_typespec>()) {
      if (parentAsPacked_array_typespec->Elem_typespec() == object) {
        // elem_type [0:n] var_name
        expectedRelation = LineColumnRelation::Before;
      }
    } else if (const UHDM::array_var* const parentAsArray_var =
                   parent->Cast<UHDM::array_var>()) {
      if (parentAsArray_var->Typespec() == object) {
        // elem_type var_name[range]
        // For array_var/Typespec, the range is the location
        if (parentAsArray_var->VpiArrayType() == vpiQueueArray) {
          // In the case of declaration, i.e. <type> <var_name[$] it is 'After'
          // In the case of assignment to empty queue, it is overlapping i.e
          //    <var_name> = {}
          expectedRelation =
              ((csl == psl) && (csc == psc) && (cel == pel) && (cec == pec))
                  ? LineColumnRelation::Inside
                  : LineColumnRelation::After;
        } else {
          expectedRelation = LineColumnRelation::After;
        }
      }
    } else if (const UHDM::array_typespec* const parentAsArray_typespec =
                   parent->Cast<UHDM::array_typespec>()) {
      if (parentAsArray_typespec->Index_typespec() == object) {
        // Since array_typspec refers to the range, index is basically the
        // range in case of associative arrays, queues, and dynamic arrays.
        expectedRelation = LineColumnRelation::Inside;
      }
    }
  } else if (object->UhdmType() == UHDM::uhdmattribute) {
    expectedRelation = LineColumnRelation::Before;
  } else if (object->UhdmType() == UHDM::uhdmrange) {
    if (const UHDM::array_typespec* const parentAsArray_typespec =
            parent->Cast<UHDM::array_typespec>()) {
      if (!parentAsArray_typespec->VpiName().empty() &&
          (parentAsArray_typespec->VpiName() != SymbolTable::getBadSymbol())) {
        // typedef int var_name[range];
        expectedRelation = LineColumnRelation::After;
      }
    } else if (parent->Cast<UHDM::io_decl>() != nullptr) {
      // (int var_name[range])
      expectedRelation = LineColumnRelation::After;
    } else if (parent->Cast<UHDM::module_array>() != nullptr) {
      // (module_type var_name[range])
      expectedRelation = LineColumnRelation::After;
    } else if (parent->Cast<UHDM::array_var>() != nullptr) {
      // int var_name[range]
      expectedRelation = LineColumnRelation::After;
    } else if (parent->Cast<UHDM::array_net>() != nullptr) {
      // some_type var_name[range]
      expectedRelation = LineColumnRelation::After;
    } else if (parent->Cast<UHDM::packed_array_var>() != nullptr) {
      // elem_type [range] var_name
      expectedRelation = LineColumnRelation::Before;
    } else if (parent->Cast<UHDM::logic_var>() != nullptr) {
      // logic [range] var_name
      expectedRelation = LineColumnRelation::Before;
    }
  } else if (object->UhdmType() == UHDM::uhdmattribute) {
    if (parent->Cast<UHDM::class_defn>() != nullptr) {
      // (* attribute *) class class_name;
      expectedRelation = LineColumnRelation::Inside;
    } else if (parent->Cast<UHDM::module_inst>() != nullptr) {
      // (* attribute *) module module_name;
      expectedRelation = LineColumnRelation::Inside;
    } else if (parent->Cast<UHDM::interface_inst>() != nullptr) {
      // (* attribute *) interface interface_name;
      expectedRelation = LineColumnRelation::Inside;
    } else if (parent->Cast<UHDM::primitive>() != nullptr) {
      // (* attribute *) primitive primitive_name;
      expectedRelation = LineColumnRelation::Inside;
    } else if (parent->Cast<UHDM::package>() != nullptr) {
      // (* attribute *) package package_name;
      expectedRelation = LineColumnRelation::Inside;
    }
  } else if (object->UhdmType() == UHDM::uhdmseq_formal_decl) {
    if (const UHDM::let_decl* const parentAsLet_decl =
            parent->Cast<UHDM::let_decl>()) {
      if (const UHDM::VectorOfseq_formal_decl* const decls =
              parentAsLet_decl->Seq_formal_decls()) {
        for (const UHDM::seq_formal_decl* const decl : *decls) {
          if (decl == object) {
            // let <name>(<..., decl, ...>) = <object>
            expectedRelation = LineColumnRelation::After;
            break;
          }
        }
      }
    }
  } else if (object->UhdmType() == UHDM::uhdmport) {
    if ((parent->Cast<UHDM::ref_module>() != nullptr) ||
        (parent->Cast<UHDM::module_array>() != nullptr)) {
      // module_type module_name(..., port, ...)
      expectedRelation = LineColumnRelation::After;
    }
  }

  if (const UHDM::event_control* parentAsEvent_control =
          parent->Cast<UHDM::event_control>()) {
    if (parentAsEvent_control->Stmt() == object) {
      // always @(....) begin ... end
      expectedRelation = LineColumnRelation::After;
    }
  } else if (const UHDM::io_decl* const parentAsIo_decl =
                 parent->Cast<UHDM::io_decl>()) {
    if (parentAsIo_decl->Expr() == object) {
      // io_decl::expr represent the default value which is
      // on the right of the variable!
      expectedRelation = LineColumnRelation::After;
    }
  } else if (const UHDM::port* const parentAsPort =
                 parent->Cast<UHDM::port>()) {
    if (parentAsPort->High_conn() == object) {
      // module modname(..., input type name = object, ... )
      expectedRelation = LineColumnRelation::After;
    }
  } else if (const UHDM::let_decl* const parentAsLet_decl =
                 parent->Cast<UHDM::let_decl>()) {
    if (const UHDM::VectorOfexpr* const exprs =
            parentAsLet_decl->Expressions()) {
      for (const UHDM::expr* const expr : *exprs) {
        if (expr == object) {
          // let <name>(<args>) = <object>
          expectedRelation = LineColumnRelation::After;
          break;
        }
      }
    }
  }

  if (actualRelation != expectedRelation) {
    if ((actualRelation == LineColumnRelation::After) &&
        (expectedRelation == LineColumnRelation::Before) &&
        (object->UhdmType() == UHDM::uhdmref_typespec) &&
        (parent->UhdmType() == UHDM::uhdmport)) {
      // typespec for ports *can* be inside the parent module!
      // module (port_name):
      //   input int port_name;
      // endmodule
      const UHDM::any* const grandParent = parent->VpiParent();

      const uint32_t psl = grandParent->VpiLineNo();
      const uint32_t psc = grandParent->VpiColumnNo();
      const uint32_t pel = grandParent->VpiEndLineNo();
      const uint32_t pec = grandParent->VpiEndColumnNo();

      actualRelation =
          getLineColumnRelation(csl, csc, cel, cec, psl, psc, pel, pec);
      expectedRelation = LineColumnRelation::Inside;
    } else if ((actualRelation == LineColumnRelation::Inside) &&
               (expectedRelation == LineColumnRelation::After) &&
               (parent->UhdmType() == UHDM::uhdmport)) {
      // unnamed port arguments for ref_module
      // module_type module_name(..., port, ...)

      if ((csl == psl) && (csc == psc) && (cel == pel) && (cec == pec)) {
        if (const UHDM::port* const parentAsPort = parent->Cast<UHDM::port>()) {
          if (parentAsPort->High_conn() == object) {
            expectedRelation = LineColumnRelation::Inside;
          }
        }
      }
    }
  }

  if (actualRelation != expectedRelation) {
    Location loc(
        fileSystem->toPathId(object->VpiFile(), symbolTable),
        object->VpiLineNo(), object->VpiColumnNo(),
        symbolTable->registerSymbol(StrCat(
            "Child: ", object->UhdmId(), ", ",
            UHDM::UhdmName(object->UhdmType()), " Parent: ", parent->UhdmId(),
            ", ", UHDM::UhdmName(parent->UhdmType()))));
    errorContainer->addError(
        ErrorDefinition::INTEGRITY_CHECK_BAD_RELATIVE_LOCATION, loc);
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
  if (parent == nullptr) return;

  const UHDM::any* const grandParent = parent->VpiParent();
  if (grandParent == nullptr) return;

  // begin in function body are implicit!
  if ((object->UhdmType() == UHDM::uhdmbegin) &&
      (parent->UhdmType() == UHDM::uhdmfunction))
    return;

  if ((object->UhdmType() == UHDM::uhdmref_typespec) &&
      (grandParent->VpiName() == "new") &&
      (parent->Cast<UHDM::variables>() != nullptr) &&
      (grandParent->UhdmType() == UHDM::uhdmfunction)) {
    // For ref_typespec associated with a class's constructor return value
    // there is no legal position because the "new" operator's return value
    // is implicit.
    const UHDM::variables* const parentAsVariables =
        parent->Cast<UHDM::variables>();
    const UHDM::task_func* const grandParentAsTask_func =
        grandParent->Cast<UHDM::function>();
    if ((grandParentAsTask_func->Return() == parent) &&
        (parentAsVariables->Typespec() == object)) {
      return;
    }
  } else if ((object->UhdmType() == UHDM::uhdmclass_typespec) &&
             (parent->VpiName() == "new") &&
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
  } else if (object->Cast<UHDM::variables>() != nullptr) {
    // When no explicit return is specific, the function's name
    // is consdiered the return type's name.
    if (const UHDM::task_func* const parentAsTask_func =
            parent->Cast<UHDM::task_func>()) {
      if (parentAsTask_func->Return() == object) return;
    }
  } else if (const UHDM::constant* const objectAsConstant =
                 object->Cast<UHDM::constant>()) {
    if (const UHDM::range* const parentAsRange = parent->Cast<UHDM::range>()) {
      // The left expression of range is allowed to be zero.
      if (parentAsRange->Left_expr() == object) return;

      // The right is allowed to be zero if it's associative
      if ((parentAsRange->Right_expr() == object) &&
          (objectAsConstant->VpiValue() == "STRING:associative")) {
        return;
      }
    }
  }

  SymbolTable* const symbolTable = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  ErrorContainer* const errorContainer = m_session->getErrorContainer();

  std::string text =
      StrCat(object->UhdmId(), ", ", UHDM::UhdmName(object->UhdmType()));
  Location loc(fileSystem->toPathId(object->VpiFile(), symbolTable),
               object->VpiLineNo(), object->VpiColumnNo(),
               symbolTable->registerSymbol(text));
  errorContainer->addError(ErrorDefinition::INTEGRITY_CHECK_MISSING_LOCATION,
                           loc);
}

bool IntegrityChecker::isImplicitFunctionReturnType(const UHDM::any* object) {
  if (any_cast<UHDM::ref_typespec>(object) != nullptr) {
    object = object->VpiParent();
  }
  if (const UHDM::variables* v = any_cast<UHDM::variables>(object)) {
    if (const UHDM::function* f = object->VpiParent<UHDM::function>()) {
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
  // Implicit function return type are unnammed.
  if (isImplicitFunctionReturnType(object)) return;

  bool shouldReport = false;

  if (const UHDM::ref_obj* const objectAsRef_obj =
          object->Cast<UHDM::ref_obj>()) {
    shouldReport = (object->VpiName() == SymbolTable::getBadSymbol());
    shouldReport = shouldReport || object->VpiName().empty();

    if (const UHDM::any* const actual = objectAsRef_obj->Actual_group()) {
      shouldReport = shouldReport || !areNamedSame(object, actual);
      shouldReport = shouldReport && !isImplicitFunctionReturnType(actual);
    }

    shouldReport = shouldReport && (object->VpiName() != "super");
    shouldReport = shouldReport && (object->VpiName() != "this");
  } else if (const UHDM::ref_typespec* const objectAsRef_typespec =
                 object->Cast<const UHDM::ref_typespec*>()) {
    shouldReport = (object->VpiName() == SymbolTable::getBadSymbol());
    if (const UHDM::typedef_typespec* const parent =
            object->VpiParent<UHDM::typedef_typespec>()) {
      if (parent->Typedef_alias() != nullptr) {
        shouldReport = false;
      }
    } else if (const UHDM::any* actual =
                   objectAsRef_typespec->Actual_typespec()) {
      if ((actual->UhdmType() == UHDM::uhdmarray_typespec) ||
          (actual->UhdmType() == UHDM::uhdmbit_typespec) ||
          (actual->UhdmType() == UHDM::uhdmbyte_typespec) ||
          (actual->UhdmType() == UHDM::uhdmchandle_typespec) ||
          (actual->UhdmType() == UHDM::uhdmint_typespec) ||
          (actual->UhdmType() == UHDM::uhdminteger_typespec) ||
          (actual->UhdmType() == UHDM::uhdmlogic_typespec) ||
          (actual->UhdmType() == UHDM::uhdmlong_int_typespec) ||
          (actual->UhdmType() == UHDM::uhdmpacked_array_typespec) ||
          (actual->UhdmType() == UHDM::uhdmreal_typespec) ||
          (actual->UhdmType() == UHDM::uhdmshort_int_typespec) ||
          (actual->UhdmType() == UHDM::uhdmshort_real_typespec) ||
          (actual->UhdmType() == UHDM::uhdmstring_typespec) ||
          (actual->UhdmType() == UHDM::uhdmtime_typespec) ||
          (actual->UhdmType() == UHDM::uhdmvoid_typespec)) {
        shouldReport = false;
      } else if ((actual->UhdmType() == UHDM::uhdmenum_typespec) ||
                 (actual->UhdmType() == UHDM::uhdmstruct_typespec) ||
                 (actual->UhdmType() == UHDM::uhdmunion_typespec)) {
        shouldReport = false;
      } else if ((actual->UhdmType() == UHDM::uhdmclass_typespec) ||
                 (actual->UhdmType() == UHDM::uhdminterface_typespec) ||
                 (actual->UhdmType() == UHDM::uhdmmodule_typespec) ||
                 (actual->UhdmType() == UHDM::uhdmunsupported_typespec)) {
        shouldReport = shouldReport || object->VpiName().empty();
        shouldReport = shouldReport || !areNamedSame(object, actual);
      }
    } else {
      shouldReport = shouldReport || object->VpiName().empty();
    }
  }

  if (shouldReport) {
    SymbolTable* const symbolTable = m_session->getSymbolTable();
    FileSystem* const fileSystem = m_session->getFileSystem();
    ErrorContainer* const errorContainer = m_session->getErrorContainer();

    Location loc(fileSystem->toPathId(object->VpiFile(), symbolTable),
                 object->VpiLineNo(), object->VpiColumnNo(),
                 symbolTable->registerSymbol(std::to_string(object->UhdmId())));
    errorContainer->addError(ErrorDefinition::INTEGRITY_CHECK_MISSING_NAME,
                             loc);
  }
}

void IntegrityChecker::reportInvalidFile(const UHDM::any* const object) const {
  std::string_view filename = object->VpiFile();
  if (filename.empty() || (filename == SymbolTable::getBadSymbol())) {
    SymbolTable* const symbolTable = m_session->getSymbolTable();
    FileSystem* const fileSystem = m_session->getFileSystem();
    ErrorContainer* const errorContainer = m_session->getErrorContainer();

    Location loc(fileSystem->toPathId(object->VpiFile(), symbolTable),
                 object->VpiLineNo(), object->VpiColumnNo(),
                 symbolTable->registerSymbol(std::to_string(object->UhdmId())));
    errorContainer->addError(ErrorDefinition::INTEGRITY_CHECK_MISSING_FILE,
                             loc);
  }
}

void IntegrityChecker::reportNullActual(const UHDM::any* const object) const {
  if (isBuiltPackageOnStack(object)) return;

  bool shouldReport = false;

  if (const UHDM::ref_obj* const objectAsRef_obj =
          object->Cast<UHDM::ref_obj>()) {
    shouldReport = objectAsRef_obj->Actual_group() == nullptr;
    // Special case for $root and few others
    if (const UHDM::any* const parent = object->VpiParent()) {
      shouldReport =
          shouldReport &&
          !(((object->VpiName() == "$root") || (object->VpiName() == "size") ||
             (object->VpiName() == "delete")) &&
            (parent->UhdmType() == UHDM::uhdmhier_path));
      shouldReport =
          shouldReport && !((parent->UhdmType() == UHDM::uhdmsys_func_call) &&
                            (parent->VpiName() == "$bits"));
      shouldReport = shouldReport && (object->VpiName() != "default");
    }
  } else if (const UHDM::ref_typespec* const objectAsRef_typespec =
                 object->Cast<const UHDM::ref_typespec*>()) {
    if (const UHDM::typedef_typespec* const parent =
            object->VpiParent<UHDM::typedef_typespec>()) {
      if (parent->Typedef_alias() != nullptr) {
        shouldReport = false;
      }
    } else {
      shouldReport = objectAsRef_typespec->Actual_typespec() == nullptr;
    }
  } else if (const UHDM::ref_module* const objectAsRef_module =
                 object->Cast<const UHDM::ref_module*>()) {
    shouldReport = objectAsRef_module->Actual_instance() == nullptr;
  } else if (const UHDM::chandle_var* const objectAsChandle_var =
                 object->Cast<const UHDM::chandle_var*>()) {
    shouldReport = objectAsChandle_var->Actual_group() == nullptr;
  } else if (const UHDM::task_func* const parentAsTask_func =
                 object->VpiParent<UHDM::task_func>()) {
    if ((parentAsTask_func->Return() == object) &&
        (parentAsTask_func->VpiAccessType() == vpiDPIImportAcc)) {
      // Imported functions cannot be bound!
      shouldReport = false;
    }
  }

  if (shouldReport) {
    SymbolTable* const symbolTable = m_session->getSymbolTable();
    FileSystem* const fileSystem = m_session->getFileSystem();
    ErrorContainer* const errorContainer = m_session->getErrorContainer();

    Location loc(fileSystem->toPathId(object->VpiFile(), symbolTable),
                 object->VpiLineNo(), object->VpiColumnNo(),
                 symbolTable->registerSymbol(
                     StrCat(object->UhdmId(), ", ", object->VpiName())));
    errorContainer->addError(ErrorDefinition::INTEGRITY_CHECK_NULL_ACTUAL, loc);
  }
}

void IntegrityChecker::enterAny(const UHDM::any* const object) {
  if (isBuiltPackageOnStack(object)) return;
  if (isUVMMember(object)) return;

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
  if (const UHDM::udp_defn* const objectAsT =
          any_cast<UHDM::udp_defn>(object)) {
    reportDuplicates(object, objectAsT->Io_decls(), "Io_decls");
    reportDuplicates(object, objectAsT->Table_entrys(), "Table_entrys");
  }
  if (const UHDM::class_defn* const objectAsT =
          any_cast<UHDM::class_defn>(object)) {
    reportDuplicates(object, objectAsT->Deriveds(), "Deriveds");
    reportDuplicates(object, objectAsT->Class_typespecs(), "Class_typespecs");
    reportDuplicates(object, objectAsT->Constraints(), "Constraints");
    reportDuplicates(object, objectAsT->Task_funcs(), "Task_funcs");
  }
  if (const UHDM::class_obj* const objectAsT =
          any_cast<UHDM::class_obj>(object)) {
    reportDuplicates(object, objectAsT->Constraints(), "Constraints");
    reportDuplicates(object, objectAsT->Messages(), "Messages");
    reportDuplicates(object, objectAsT->Task_funcs(), "Task_funcs");
    reportDuplicates(object, objectAsT->Threads(), "Threads");
  }
  if (const UHDM::instance* const objectAsT =
          any_cast<UHDM::instance>(object)) {
    reportDuplicates(object, objectAsT->Array_nets(), "Array_nets");
    reportDuplicates(object, objectAsT->Assertions(), "Assertions");
    reportDuplicates(object, objectAsT->Class_defns(), "Class_defns");
    reportDuplicates(object, objectAsT->Nets(), "Nets");
    reportDuplicates(object, objectAsT->Programs(), "Programs");
    reportDuplicates(object, objectAsT->Program_arrays(), "Program_arrays");
    reportDuplicates(object, objectAsT->Spec_params(), "Spec_params");
    reportDuplicates(object, objectAsT->Task_funcs(), "Task_funcs");
  }
  if (const UHDM::checker_decl* const objectAsT =
          any_cast<UHDM::checker_decl>(object)) {
    reportDuplicates(object, objectAsT->Ports(), "Ports");
    reportDuplicates(object, objectAsT->Cont_assigns(), "Cont_assigns");
    reportDuplicates(object, objectAsT->Process(), "Process");
  }
  if (const UHDM::checker_inst* const objectAsT =
          any_cast<UHDM::checker_inst>(object)) {
    reportDuplicates(object, objectAsT->Ports(), "Ports");
  }
  if (const UHDM::interface_inst* const objectAsT =
          any_cast<UHDM::interface_inst>(object)) {
    reportDuplicates(object, objectAsT->Clocking_blocks(),
                     "Io_dClocking_blocksecls");
    reportDuplicates(object, objectAsT->Cont_assigns(), "Cont_assigns");
    reportDuplicates(object, objectAsT->Gen_scope_arrays(), "Gen_scope_arrays");
    reportDuplicates(object, objectAsT->Gen_stmts(), "Gen_stmts");
    reportDuplicates(object, objectAsT->Interface_arrays(), "Interface_arrays");
    reportDuplicates(object, objectAsT->Interfaces(), "Interfaces");
    reportDuplicates(object, objectAsT->Interface_tf_decls(),
                     "Interface_tf_decls");
    reportDuplicates(object, objectAsT->Mod_paths(), "Mod_paths");
    reportDuplicates(object, objectAsT->Modports(), "Modports");
    reportDuplicates(object, objectAsT->Ports(), "Ports");
    reportDuplicates(object, objectAsT->Process(), "Process");
    reportDuplicates(object, objectAsT->Elab_tasks(), "Elab_tasks");
  }
  if (const UHDM::module_inst* const objectAsT =
          any_cast<UHDM::module_inst>(object)) {
    reportDuplicates(object, objectAsT->Alias_stmts(), "Alias_stmts");
    reportDuplicates(object, objectAsT->Clocking_blocks(), "Clocking_blocks");
    reportDuplicates(object, objectAsT->Cont_assigns(), "Cont_assigns");
    reportDuplicates(object, objectAsT->Def_params(), "Def_params");
    reportDuplicates(object, objectAsT->Gen_scope_arrays(), "Gen_scope_arrays");
    reportDuplicates(object, objectAsT->Gen_stmts(), "Gen_stmts");
    reportDuplicates(object, objectAsT->Interface_arrays(), "Interface_arrays");
    reportDuplicates(object, objectAsT->Interfaces(), "Interfaces");
    reportDuplicates(object, objectAsT->Io_decls(), "Io_decls");
    reportDuplicates(object, objectAsT->Mod_paths(), "Mod_paths");
    reportDuplicates(object, objectAsT->Module_arrays(), "Module_arrays");
    reportDuplicates(object, objectAsT->Modules(), "Modules");
    reportDuplicates(object, objectAsT->Ports(), "Ports");
    reportDuplicates(object, objectAsT->Primitives(), "Primitives");
    reportDuplicates(object, objectAsT->Primitive_arrays(), "Primitive_arrays");
    reportDuplicates(object, objectAsT->Process(), "Process");
    reportDuplicates(object, objectAsT->Ref_modules(), "Ref_modules");
    reportDuplicates(object, objectAsT->Tchks(), "Tchks");
    reportDuplicates(object, objectAsT->Elab_tasks(), "Elab_tasks");
  }
  if (const UHDM::program* const objectAsT = any_cast<UHDM::program>(object)) {
    reportDuplicates(object, objectAsT->Clocking_blocks(), "Clocking_blocks");
    reportDuplicates(object, objectAsT->Cont_assigns(), "Cont_assigns");
    reportDuplicates(object, objectAsT->Gen_scope_arrays(), "Gen_scope_arrays");
    reportDuplicates(object, objectAsT->Interface_arrays(), "Interface_arrays");
    reportDuplicates(object, objectAsT->Interfaces(), "Interfaces");
    reportDuplicates(object, objectAsT->Ports(), "Ports");
    reportDuplicates(object, objectAsT->Process(), "Process");
  }
  if (const UHDM::multiclock_sequence_expr* const objectAsT =
          any_cast<UHDM::multiclock_sequence_expr>(object)) {
    reportDuplicates(object, objectAsT->Clocked_seqs(), "Clocked_seqs");
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

  SymbolTable* const symbolTable = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  ErrorContainer* const errorContainer = m_session->getErrorContainer();

  const UHDM::any* const parent = object->VpiParent();
  if (parent == nullptr) {
    Location loc(fileSystem->toPathId(object->VpiFile(), symbolTable),
                 object->VpiLineNo(), object->VpiColumnNo(),
                 symbolTable->registerSymbol(std::to_string(object->UhdmId())));
    errorContainer->addError(ErrorDefinition::INTEGRITY_CHECK_MISSING_PARENT,
                             loc);
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
    Location loc(fileSystem->toPathId(object->VpiFile(), symbolTable),
                 object->VpiLineNo(), object->VpiColumnNo(),
                 symbolTable->registerSymbol(std::to_string(object->UhdmId())));
    errorContainer->addError(
        ErrorDefinition::INTEGRITY_CHECK_PARENT_IS_NEITHER_SCOPE_NOR_DESIGN,
        loc);
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

void IntegrityChecker::enterAlias_stmts(
    const UHDM::any* const object, const UHDM::VectorOfalias_stmt& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterAllClasses(
    const UHDM::any* const object, const UHDM::VectorOfclass_defn& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterAllInterfaces(
    const UHDM::any* const object,
    const UHDM::VectorOfinterface_inst& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterAllModules(
    const UHDM::any* const object, const UHDM::VectorOfmodule_inst& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterAllPackages(const UHDM::any* const object,
                                        const UHDM::VectorOfpackage& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterAllPrograms(const UHDM::any* const object,
                                        const UHDM::VectorOfprogram& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterAllUdps(const UHDM::any* const object,
                                    const UHDM::VectorOfudp_defn& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterArguments(const UHDM::any* const object,
                                      const UHDM::VectorOfexpr& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterArray_nets(const UHDM::any* const object,
                                       const UHDM::VectorOfarray_net& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterArray_var_mems(
    const UHDM::any* const object, const UHDM::VectorOfarray_var& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterArray_vars(const UHDM::any* const object,
                                       const UHDM::VectorOfarray_var& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterAssertions(const UHDM::any* const object,
                                       const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterAttributes(const UHDM::any* const object,
                                       const UHDM::VectorOfattribute& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterBits(const UHDM::any* const object,
                                 const UHDM::VectorOfport_bit& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterCase_items(const UHDM::any* const object,
                                       const UHDM::VectorOfcase_item& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterCase_property_items(
    const UHDM::any* const object,
    const UHDM::VectorOfcase_property_item& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterClass_defns(
    const UHDM::any* const object, const UHDM::VectorOfclass_defn& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterClass_typespecs(
    const UHDM::any* const object,
    const UHDM::VectorOfclass_typespec& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterClocked_seqs(
    const UHDM::any* const object, const UHDM::VectorOfclocked_seq& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterClocking_blocks(
    const UHDM::any* const object,
    const UHDM::VectorOfclocking_block& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterClocking_io_decls(
    const UHDM::any* const object,
    const UHDM::VectorOfclocking_io_decl& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterConcurrent_assertions(
    const UHDM::any* const object,
    const UHDM::VectorOfconcurrent_assertions& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterConstraint_exprs(
    const UHDM::any* const object,
    const UHDM::VectorOfconstraint_expr& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterConstraint_items(const UHDM::any* const object,
                                             const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterConstraints(
    const UHDM::any* const object, const UHDM::VectorOfconstraint& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterCont_assign_bits(
    const UHDM::any* const object,
    const UHDM::VectorOfcont_assign_bit& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterCont_assigns(
    const UHDM::any* const object, const UHDM::VectorOfcont_assign& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterDef_params(const UHDM::any* const object,
                                       const UHDM::VectorOfdef_param& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterDeriveds(const UHDM::any* const object,
                                     const UHDM::VectorOfclass_defn& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterDist_items(const UHDM::any* const object,
                                       const UHDM::VectorOfdist_item& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterDrivers(const UHDM::any* const object,
                                    const UHDM::VectorOfnet_drivers& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterElab_tasks(const UHDM::any* const object,
                                       const UHDM::VectorOftf_call& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterElements(const UHDM::any* const object,
                                     const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterElse_constraint_exprs(
    const UHDM::any* const object,
    const UHDM::VectorOfconstraint_expr& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterEnum_consts(
    const UHDM::any* const object, const UHDM::VectorOfenum_const& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterExpr_indexes(const UHDM::any* const object,
                                         const UHDM::VectorOfexpr& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterExpr_tchk_terms(const UHDM::any* const object,
                                            const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterExpressions(const UHDM::any* const object,
                                        const UHDM::VectorOfexpr& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterExprs(const UHDM::any* const object,
                                  const UHDM::VectorOfexpr& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterFunctions(const UHDM::any* const object,
                                      const UHDM::VectorOffunction& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterGen_scope_arrays(
    const UHDM::any* const object,
    const UHDM::VectorOfgen_scope_array& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterGen_scopes(const UHDM::any* const object,
                                       const UHDM::VectorOfgen_scope& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterGen_stmts(const UHDM::any* const object,
                                      const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterIncludes(const UHDM::any* const object,
                                     const UHDM::VectorOfsource_file& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterIndexes(const UHDM::any* const object,
                                    const UHDM::VectorOfexpr& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterInstance_items(const UHDM::any* const object,
                                           const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterInstances(const UHDM::any* const object,
                                      const UHDM::VectorOfinstance& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterInterface_arrays(
    const UHDM::any* const object,
    const UHDM::VectorOfinterface_array& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterInterface_tf_decls(
    const UHDM::any* const object,
    const UHDM::VectorOfinterface_tf_decl& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterInterfaces(
    const UHDM::any* const object,
    const UHDM::VectorOfinterface_inst& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterIo_decls(const UHDM::any* const object,
                                     const UHDM::VectorOfio_decl& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterLet_decls(const UHDM::any* const object,
                                      const UHDM::VectorOflet_decl& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterLoads(const UHDM::any* const object,
                                  const UHDM::VectorOfnet_loads& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterLocal_drivers(
    const UHDM::any* const object, const UHDM::VectorOfnet_drivers& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterLocal_loads(
    const UHDM::any* const object, const UHDM::VectorOfnet_loads& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterLogic_vars(const UHDM::any* const object,
                                       const UHDM::VectorOflogic_var& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterMembers(
    const UHDM::any* const object,
    const UHDM::VectorOftypespec_member& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterMessages(const UHDM::any* const object,
                                     const UHDM::VectorOfexpr& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterMod_paths(const UHDM::any* const object,
                                      const UHDM::VectorOfmod_path& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterModports(const UHDM::any* const object,
                                     const UHDM::VectorOfmodport& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterModule_arrays(
    const UHDM::any* const object, const UHDM::VectorOfmodule_array& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterModules(const UHDM::any* const object,
                                    const UHDM::VectorOfmodule_inst& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterNamed_event_arrays(
    const UHDM::any* const object,
    const UHDM::VectorOfnamed_event_array& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterNamed_event_sequence_expr_groups(
    const UHDM::any* const object, const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterNamed_events(
    const UHDM::any* const object, const UHDM::VectorOfnamed_event& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterNet_bits(const UHDM::any* const object,
                                     const UHDM::VectorOfnet_bit& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterNets(const UHDM::any* const object,
                                 const UHDM::VectorOfnet& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterNets(const UHDM::any* const object,
                                 const UHDM::VectorOfnets& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterOperands(const UHDM::any* const object,
                                     const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterParam_assigns(
    const UHDM::any* const object, const UHDM::VectorOfparam_assign& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterParameters(const UHDM::any* const object,
                                       const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterPath_elems(const UHDM::any* const object,
                                       const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterPath_terms(const UHDM::any* const object,
                                       const UHDM::VectorOfpath_term& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterPorts(
    const UHDM::any* const object,
    const UHDM::VectorOfchecker_inst_port& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterPorts(const UHDM::any* const object,
                                  const UHDM::VectorOfchecker_port& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterPorts(const UHDM::any* const object,
                                  const UHDM::VectorOfport& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterPorts(const UHDM::any* const object,
                                  const UHDM::VectorOfports& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterPreproc_macro_definitions(
    const UHDM::any* const object,
    const UHDM::VectorOfpreproc_macro_definition& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterPreproc_macro_instances(
    const UHDM::any* const object,
    const UHDM::VectorOfpreproc_macro_instance& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterPrim_terms(const UHDM::any* const object,
                                       const UHDM::VectorOfprim_term& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterPrimitive_arrays(
    const UHDM::any* const object,
    const UHDM::VectorOfprimitive_array& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterPrimitives(const UHDM::any* const object,
                                       const UHDM::VectorOfprimitive& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterProcess(const UHDM::any* const object,
                                    const UHDM::VectorOfprocess_stmt& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterProgram_arrays(
    const UHDM::any* const object, const UHDM::VectorOfprogram& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterProgram_arrays(
    const UHDM::any* const object, const UHDM::VectorOfprogram_array& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterPrograms(const UHDM::any* const object,
                                     const UHDM::VectorOfprogram& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterProp_formal_decls(
    const UHDM::any* const object,
    const UHDM::VectorOfprop_formal_decl& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterProperty_decls(
    const UHDM::any* const object, const UHDM::VectorOfproperty_decl& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterRanges(const UHDM::any* const object,
                                   const UHDM::VectorOfrange& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterRef_modules(
    const UHDM::any* const object, const UHDM::VectorOfref_module& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterRegs(const UHDM::any* const object,
                                 const UHDM::VectorOfreg& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterScopes(const UHDM::any* const object,
                                   const UHDM::VectorOfscope& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterSeq_formal_decls(
    const UHDM::any* const object,
    const UHDM::VectorOfseq_formal_decl& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterSequence_decls(
    const UHDM::any* const object, const UHDM::VectorOfsequence_decl& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterSolve_afters(const UHDM::any* const object,
                                         const UHDM::VectorOfexpr& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterSolve_befores(const UHDM::any* const object,
                                          const UHDM::VectorOfexpr& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterSource_files(
    const UHDM::any* const object, const UHDM::VectorOfsource_file& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterSpec_params(
    const UHDM::any* const object, const UHDM::VectorOfspec_param& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterStmts(const UHDM::any* const object,
                                  const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterTable_entrys(
    const UHDM::any* const object, const UHDM::VectorOftable_entry& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterTask_func_decls(
    const UHDM::any* const object,
    const UHDM::VectorOftask_func_decl& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterTask_funcs(const UHDM::any* const object,
                                       const UHDM::VectorOftask_func& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterTasks(const UHDM::any* const object,
                                  const UHDM::VectorOftask& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterTchk_terms(const UHDM::any* const object,
                                       const UHDM::VectorOftchk_term& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterTchks(const UHDM::any* const object,
                                  const UHDM::VectorOftchk& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterTf_call_args(const UHDM::any* const object,
                                         const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterThreads(const UHDM::any* const object,
                                    const UHDM::VectorOfthread_obj& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterTopModules(
    const UHDM::any* const object, const UHDM::VectorOfmodule_inst& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterTopPackages(const UHDM::any* const object,
                                        const UHDM::VectorOfpackage& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterTypespecs(const UHDM::any* const object,
                                      const UHDM::VectorOftypespec& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterVar_bits(const UHDM::any* const object,
                                     const UHDM::VectorOfvar_bit& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterVar_selects(
    const UHDM::any* const object, const UHDM::VectorOfvar_select& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterVariable_drivers(const UHDM::any* const object,
                                             const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterVariable_loads(const UHDM::any* const object,
                                           const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterVariables(const UHDM::any* const object,
                                      const UHDM::VectorOfvariables& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterVirtual_interface_vars(
    const UHDM::any* const object,
    const UHDM::VectorOfvirtual_interface_var& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterVpiArguments(const UHDM::any* const object,
                                         const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterVpiConditions(const UHDM::any* const object,
                                          const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterVpiExprs(const UHDM::any* const object,
                                     const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterVpiForIncStmts(const UHDM::any* const object,
                                           const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterVpiForInitStmts(const UHDM::any* const object,
                                            const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterVpiLoopVars(const UHDM::any* const object,
                                        const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
}
void IntegrityChecker::enterVpiUses(const UHDM::any* const object,
                                    const UHDM::VectorOfany& objects) {
  reportDuplicates(object, &objects, "");
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
