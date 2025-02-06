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
 * File:   ObjectBinder.cpp
 * Author: hs
 *
 * Created on August 10, 2024, 00:00 AM
 */

#include <Surelog/Common/FileSystem.h>
#include <Surelog/Common/Session.h>
#include <Surelog/Design/DataType.h>
#include <Surelog/Design/DesignComponent.h>
#include <Surelog/Design/DesignElement.h>
#include <Surelog/Design/FileContent.h>
#include <Surelog/Design/ModuleDefinition.h>
#include <Surelog/Design/ModuleInstance.h>
#include <Surelog/DesignCompile/ObjectBinder.h>
#include <Surelog/DesignCompile/UhdmWriter.h>
#include <Surelog/ErrorReporting/ErrorContainer.h>
#include <Surelog/SourceCompile/SymbolTable.h>
#include <Surelog/Testbench/ClassDefinition.h>
#include <Surelog/Testbench/TypeDef.h>
#include <Surelog/Utils/StringUtils.h>

// uhdm
#include <uhdm/Serializer.h>
#include <uhdm/uhdm.h>

namespace SURELOG {
ObjectBinder::ObjectBinder(Session* session, const ComponentMap& componentMap,
                           UHDM::Serializer& serializer, bool muteStdout)
    : m_session(session),
      m_componentMap(componentMap),
      m_serializer(serializer),
      m_muteStdout(muteStdout) {
  for (ComponentMap::const_reference entry : m_componentMap) {
    m_baseclassMap.emplace(entry.second, entry.first);
  }
}

inline bool ObjectBinder::areSimilarNames(std::string_view name1,
                                          std::string_view name2) const {
  size_t pos = name1.find("::");
  if (pos != std::string::npos) {
    name1 = name1.substr(pos + 2);
  }

  pos = name1.find("work@");
  if (pos != std::string::npos) {
    name1 = name1.substr(pos + 5);
  }

  pos = name2.find("::");
  if (pos != std::string::npos) {
    name2 = name2.substr(pos + 2);
  }

  pos = name2.find("work@");
  if (pos != std::string::npos) {
    name2 = name2.substr(pos + 5);
  }

  return !name1.empty() && name1 == name2;
}

inline bool ObjectBinder::areSimilarNames(const UHDM::any* const object1,
                                          std::string_view name2) const {
  return areSimilarNames(object1->VpiName(), name2);
}

inline bool ObjectBinder::areSimilarNames(
    const UHDM::any* const object1, const UHDM::any* const object2) const {
  return areSimilarNames(object1->VpiName(), object2->VpiName());
}

bool ObjectBinder::isInElaboratedTree(const UHDM::any* const object) {
  const UHDM::any* p = object;
  while (p != nullptr) {
    if (const UHDM::instance* const inst = any_cast<UHDM::instance>(p)) {
      if (inst->VpiTop()) return true;
    }
    p = p->VpiParent();
  }
  return false;
}

VObjectType ObjectBinder::getDefaultNetType(
    const ValuedComponentI* component) const {
  if (component == nullptr) return VObjectType::slNoType;

  if (const DesignComponent* dc1 =
          valuedcomponenti_cast<DesignComponent>(component)) {
    if (const DesignElement* de = dc1->getDesignElement()) {
      return de->m_defaultNetType;
    }
  }

  if (const ModuleInstance* mi =
          valuedcomponenti_cast<ModuleInstance>(component)) {
    if (const DesignComponent* dc2 =
            valuedcomponenti_cast<DesignComponent>(mi->getDefinition())) {
      if (const DesignElement* de = dc2->getDesignElement()) {
        return de->m_defaultNetType;
      }
    }
  }

  return VObjectType::slNoType;
}

const UHDM::package* ObjectBinder::getPackage(std::string_view name) const {
  if (m_designStack.empty()) return nullptr;

  const UHDM::design* const d = m_designStack.back();
  if (d->AllPackages() != nullptr) {
    for (const UHDM::package* p : *d->AllPackages()) {
      if (areSimilarNames(p, name)) {
        return p;
      }
    }
  }

  return nullptr;
}

const UHDM::module_inst* ObjectBinder::getModuleInst(
    std::string_view defname) const {
  if (m_designStack.empty()) return nullptr;

  const UHDM::design* const d = m_designStack.back();
  if (d->AllModules() != nullptr) {
    for (const UHDM::module_inst* m : *d->AllModules()) {
      if (m->VpiDefName() == defname) {
        return m;
      }
    }
  }

  return nullptr;
}

const UHDM::interface_inst* ObjectBinder::getInterfaceInst(
    std::string_view defname) const {
  if (m_designStack.empty()) return nullptr;

  const UHDM::design* const d = m_designStack.back();
  if (d->AllInterfaces() != nullptr) {
    for (const UHDM::interface_inst* m : *d->AllInterfaces()) {
      if (m->VpiDefName() == defname) {
        return m;
      }
    }
  }

  return nullptr;
}

const UHDM::class_defn* ObjectBinder::getClass_defn(
    const UHDM::VectorOfclass_defn* const collection,
    std::string_view name) const {
  if (collection != nullptr) {
    for (const UHDM::class_defn* c : *collection) {
      if (areSimilarNames(c, name)) {
        return c;
      }
    }
  }
  return nullptr;
}

const UHDM::class_defn* ObjectBinder::getClass_defn(
    std::string_view name) const {
  if (!m_packageStack.empty()) {
    const UHDM::package* const p = m_packageStack.back();
    if (const UHDM::class_defn* const c =
            getClass_defn(p->Class_defns(), name)) {
      return c;
    }
  }
  if (!m_designStack.empty()) {
    const UHDM::design* const d = m_designStack.back();
    if (const UHDM::class_defn* const c =
            getClass_defn(d->AllClasses(), name)) {
      return c;
    }

    if (d->Typespecs() != nullptr) {
      for (const UHDM::typespec* const t : *d->Typespecs()) {
        if (const UHDM::import_typespec* const it =
                t->Cast<UHDM::import_typespec>()) {
          if (const UHDM::package* p = getPackage(it->VpiName())) {
            if (const UHDM::class_defn* const c =
                    getClass_defn(p->Class_defns(), name)) {
              return c;
            }
          }
        }
      }
    }
  }

  return nullptr;
}

void ObjectBinder::enterDesign(const UHDM::design* const object) {
  m_designStack.emplace_back(object);
}

void ObjectBinder::leaveDesign(const UHDM::design* const object) {
  m_designStack.pop_back();
}

void ObjectBinder::enterPackage(const UHDM::package* const object) {
  m_packageStack.emplace_back(object);
}

void ObjectBinder::leavePackage(const UHDM::package* const object) {
  m_packageStack.pop_back();
}

void ObjectBinder::enterHier_path(const UHDM::hier_path* const object) {
  m_prefixStack.emplace_back(object);
}

void ObjectBinder::leaveHier_path(const UHDM::hier_path* const object) {
  if (!m_prefixStack.empty() && (m_prefixStack.back() == object)) {
    m_prefixStack.pop_back();
  }
}

const UHDM::any* ObjectBinder::findInTypespec(
    std::string_view name, const UHDM::any* const object,
    const UHDM::typespec* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  switch (scope->UhdmType()) {
    case UHDM::uhdmenum_typespec: {
      if (const UHDM::any* const actual = findInVectorOfAny(
              name, object,
              static_cast<const UHDM::enum_typespec*>(scope)->Enum_consts(),
              scope)) {
        return actual;
      }
    } break;

    case UHDM::uhdmstruct_typespec: {
      if (const UHDM::any* const actual = findInVectorOfAny(
              name, object,
              static_cast<const UHDM::struct_typespec*>(scope)->Members(),
              scope)) {
        return actual;
      }
    } break;

    case UHDM::uhdmunion_typespec: {
      if (const UHDM::any* const actual = findInVectorOfAny(
              name, object,
              static_cast<const UHDM::union_typespec*>(scope)->Members(),
              scope)) {
        return actual;
      }
    } break;

    case UHDM::uhdmimport_typespec: {
      const UHDM::import_typespec* const it =
          any_cast<UHDM::import_typespec>(scope);
      if (const UHDM::package* const p = getPackage(it->VpiName())) {
        if (const UHDM::any* const actual = findInPackage(name, object, p)) {
          return actual;
        }
      }
    } break;

    case UHDM::uhdmclass_typespec: {
      if (const UHDM::class_defn* cd =
              static_cast<const UHDM::class_typespec*>(scope)->Class_defn()) {
        if (const UHDM::any* const actual =
                findInClass_defn(name, object, cd)) {
          return actual;
        }
      }
    } break;

    case UHDM::uhdminterface_typespec: {
      if (const UHDM::interface_inst* ins =
              static_cast<const UHDM::interface_typespec*>(scope)
                  ->Interface_inst()) {
        if (const UHDM::any* const actual =
                findInInterface_inst(name, object, ins)) {
          return actual;
        }
      }
    } break;

    default:
      break;
  }

  return nullptr;
}

const UHDM::any* ObjectBinder::findInRefTypespec(
    std::string_view name, const UHDM::any* const object,
    const UHDM::ref_typespec* const scope) {
  if (scope == nullptr) return nullptr;

  if (const UHDM::typespec* const ts = scope->Actual_typespec()) {
    return findInTypespec(name, object, ts);
  }
  return nullptr;
}

template <typename T>
const UHDM::any* ObjectBinder::findInVectorOfAny(
    std::string_view name, const UHDM::any* const object,
    const std::vector<T*>* const collection, const UHDM::any* const scope) {
  if (collection == nullptr) return nullptr;

  std::string_view shortName = name;
  if (shortName.find("::") != std::string::npos) {
    std::vector<std::string_view> tokens;
    StringUtils::tokenizeMulti(shortName, "::", tokens);
    if (tokens.size() > 1) shortName = tokens.back();
  }

  for (const UHDM::any* c : *collection) {
    if (c->UhdmType() == UHDM::uhdmunsupported_typespec) continue;
    if (c->UhdmType() == UHDM::uhdmunsupported_stmt) continue;
    if (c->UhdmType() == UHDM::uhdmunsupported_expr) continue;
    if (c->UhdmType() == UHDM::uhdmref_module) continue;
    if (c->UhdmType() == UHDM::uhdmvar_select) continue;
    if (any_cast<UHDM::ref_obj>(c) != nullptr) continue;

    if (any_cast<UHDM::typespec>(c) == nullptr) {
      if (any_cast<UHDM::ref_obj>(object) != nullptr) {
        if (areSimilarNames(c, name)) return c;
        if (areSimilarNames(c, shortName)) return c;
      }
    } else {
      if (any_cast<UHDM::ref_typespec>(object) != nullptr) {
        if (areSimilarNames(c, name)) return c;
        if (areSimilarNames(c, shortName)) return c;
      }
    }

    if (any_cast<UHDM::typespec>(c) != nullptr) {
      if (const UHDM::any* const actual = findInTypespec(
              name, object, static_cast<const UHDM::typespec*>(c))) {
        return actual;
      }
    }

    if (c->UhdmType() == UHDM::uhdmenum_var) {
      if (const UHDM::any* const actual = findInRefTypespec(
              name, object,
              static_cast<const UHDM::enum_var*>(c)->Typespec())) {
        return actual;
      } else if (const UHDM::any* const actual = findInRefTypespec(
                     shortName, object,
                     static_cast<const UHDM::enum_var*>(c)->Typespec())) {
        return actual;
      }
    }
    // if (c->UhdmType() == UHDM::uhdmstruct_var) {
    //   if (const UHDM::any* const actual = findInRefTypespec(
    //           name, static_cast<const UHDM::struct_var*>(c)->Typespec())) {
    //     return actual;
    //   }
    // }
    if (const UHDM::ref_typespec* rt = any_cast<UHDM::ref_typespec>(c)) {
      if (scope != rt->Actual_typespec()) {
        if (const UHDM::any* const actual =
                findInRefTypespec(name, object, rt)) {
          return actual;
        } else if (const UHDM::any* const actual =
                       findInRefTypespec(shortName, object, rt)) {
          return actual;
        }
      }
    }
  }

  return nullptr;
}

const UHDM::any* ObjectBinder::findInScope(std::string_view name,
                                           const UHDM::any* const object,
                                           const UHDM::scope* const scope) {
  if (scope == nullptr) return nullptr;

  if (const UHDM::any* const actual =
          findInVectorOfAny(name, object, scope->Variables(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual = findInVectorOfAny(
                 name, object, scope->Param_assigns(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Parameters(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual = findInVectorOfAny(
                 name, object, scope->Property_decls(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Typespecs(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual = findInVectorOfAny(
                 name, object, scope->Named_events(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Scopes(), scope)) {
    return actual;
  } else if (const UHDM::package* const p = any_cast<UHDM::package>(scope)) {
    std::string fullName = StrCat(p->VpiName(), "::", name);
    if (const UHDM::any* const actual =
            findInVectorOfAny(fullName, object, scope->Typespecs(), scope)) {
      return actual;
    }
  } else if (const UHDM::any* const actual = findInVectorOfAny(
                 name, object, scope->Instance_items(), scope)) {
    return actual;
  }

  return nullptr;
}

const UHDM::any* ObjectBinder::findInInstance(
    std::string_view name, const UHDM::any* const object,
    const UHDM::instance* const scope) {
  if (scope == nullptr) return nullptr;

  if (areSimilarNames(scope, name)) {
    return scope;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Nets(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Array_nets(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Task_funcs(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Programs(), scope)) {
    return actual;
  }

  return findInScope(name, object, scope);
}

const UHDM::any* ObjectBinder::findInInterface_inst(
    std::string_view name, const UHDM::any* const object,
    const UHDM::interface_inst* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if (areSimilarNames(scope, name)) {
    return scope;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Modports(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual = findInVectorOfAny(
                 name, object, scope->Interface_tf_decls(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Ports(), scope)) {
    return actual;
  }
  return findInInstance(name, object, scope);
}

const UHDM::any* ObjectBinder::findInPackage(std::string_view name,
                                             const UHDM::any* const object,
                                             const UHDM::package* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if (areSimilarNames(scope, name)) {
    return scope;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Parameters(), scope)) {
    return actual;
  }

  return findInInstance(name, object, scope);
}

const UHDM::any* ObjectBinder::findInUdp_defn(
    std::string_view name, const UHDM::any* const object,
    const UHDM::udp_defn* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if (areSimilarNames(scope, name)) return scope;

  return findInVectorOfAny(name, object, scope->Io_decls(), scope);
}

const UHDM::any* ObjectBinder::findInProgram(std::string_view name,
                                             const UHDM::any* const object,
                                             const UHDM::program* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if (areSimilarNames(scope, name)) {
    return scope;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Parameters(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Ports(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Interfaces(), scope)) {
    return actual;
  }

  return findInInstance(name, object, scope);
}

const UHDM::any* ObjectBinder::findInFunction(
    std::string_view name, const UHDM::any* const object,
    const UHDM::function* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if (areSimilarNames(scope, name)) {
    return scope->Return();
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Io_decls(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Variables(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Parameters(), scope)) {
    return actual;
  } else if (const UHDM::package* const inst =
                 scope->Instance<UHDM::package>()) {
    if (const UHDM::any* const actual = findInPackage(name, object, inst)) {
      return actual;
    }
  }

  return findInScope(name, object, scope);
}

const UHDM::any* ObjectBinder::findInTask(std::string_view name,
                                          const UHDM::any* const object,
                                          const UHDM::task* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if (areSimilarNames(scope, name)) {
    return scope;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Io_decls(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Variables(), scope)) {
    return actual;
  } else if (const UHDM::package* const p = scope->Instance<UHDM::package>()) {
    if (const UHDM::any* const actual = findInPackage(name, object, p)) {
      return actual;
    }
  }

  return findInScope(name, object, scope);
}

const UHDM::any* ObjectBinder::findInFor_stmt(
    std::string_view name, const UHDM::any* const object,
    const UHDM::for_stmt* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  std::string_view shortName = name;
  if (shortName.find("::") != std::string::npos) {
    std::vector<std::string_view> tokens;
    StringUtils::tokenizeMulti(shortName, "::", tokens);
    if (tokens.size() > 1) shortName = tokens.back();
  }

  if (const UHDM::VectorOfany* const inits = scope->VpiForInitStmts()) {
    for (auto init : *inits) {
      if (init->UhdmType() == UHDM::uhdmassign_stmt) {
        const UHDM::expr* const lhs =
            static_cast<const UHDM::assign_stmt*>(init)->Lhs();
        if (lhs->UhdmType() == UHDM::uhdmunsupported_typespec) continue;
        if (lhs->UhdmType() == UHDM::uhdmunsupported_stmt) continue;
        if (lhs->UhdmType() == UHDM::uhdmunsupported_expr) continue;
        if (lhs->UhdmType() == UHDM::uhdmref_module) continue;
        if (lhs->UhdmType() == UHDM::uhdmvar_select) continue;
        if (any_cast<UHDM::ref_obj>(lhs) != nullptr) continue;
        if (areSimilarNames(lhs, name)) return lhs;
        if (areSimilarNames(lhs, shortName)) return lhs;
      }
    }
  }

  return findInScope(name, object, scope);
}

const UHDM::any* ObjectBinder::findInForeach_stmt(
    std::string_view name, const UHDM::any* const object,
    const UHDM::foreach_stmt* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if (const UHDM::any* const var =
          findInVectorOfAny(name, object, scope->VpiLoopVars(), scope)) {
    return var;
  }

  return findInScope(name, object, scope);
}

template <typename T>
const UHDM::any* ObjectBinder::findInScope_sub(
    std::string_view name, const UHDM::any* const object, const T* const scope,
    typename std::enable_if<
        std::is_same<UHDM::begin, typename std::decay<T>::type>::value ||
        std::is_same<UHDM::fork_stmt,
                     typename std::decay<T>::type>::value>::type* /* = 0 */) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  std::string_view shortName = name;
  if (shortName.find("::") != std::string::npos) {
    std::vector<std::string_view> tokens;
    StringUtils::tokenizeMulti(shortName, "::", tokens);
    if (tokens.size() > 1) shortName = tokens.back();
  }

  if (areSimilarNames(scope, name) || areSimilarNames(scope, shortName)) {
    return scope;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Variables(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Parameters(), scope)) {
    return actual;
  }

  if (const UHDM::VectorOfany* const stmts = scope->Stmts()) {
    for (auto init : *stmts) {
      if (init->UhdmType() == UHDM::uhdmassign_stmt) {
        const UHDM::expr* const lhs =
            static_cast<const UHDM::assign_stmt*>(init)->Lhs();
        if (lhs->UhdmType() == UHDM::uhdmunsupported_typespec) continue;
        if (lhs->UhdmType() == UHDM::uhdmunsupported_stmt) continue;
        if (lhs->UhdmType() == UHDM::uhdmunsupported_expr) continue;
        if (lhs->UhdmType() == UHDM::uhdmref_module) continue;
        if (lhs->UhdmType() == UHDM::uhdmvar_select) continue;
        if (any_cast<UHDM::ref_obj>(lhs) != nullptr) continue;
        if (areSimilarNames(lhs, name)) return lhs;
        if (areSimilarNames(lhs, shortName)) return lhs;
      }
    }
  }

  return findInScope(name, object, scope);
}

const UHDM::any* ObjectBinder::findInClass_defn(
    std::string_view name, const UHDM::any* const object,
    const UHDM::class_defn* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  std::string_view shortName = name;
  if (shortName.find("::") != std::string::npos) {
    std::vector<std::string_view> tokens;
    StringUtils::tokenizeMulti(shortName, "::", tokens);
    if (tokens.size() > 1) shortName = tokens.back();
  }

  if (areSimilarNames(name, "this")) {
    return scope;
  } else if (areSimilarNames(name, "super")) {
    if (const UHDM::extends* ext =
            static_cast<const UHDM::class_defn*>(scope)->Extends()) {
      if (const UHDM::ref_typespec* rt = ext->Class_typespec()) {
        if (const UHDM::class_typespec* cts =
                rt->Actual_typespec<UHDM::class_typespec>())
          return cts->Class_defn();
      }
    }
    return nullptr;
  }

  if (areSimilarNames(scope, name) || areSimilarNames(scope, shortName)) {
    return scope;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Variables(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Task_funcs(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual = findInScope(
                 name, object, static_cast<const UHDM::scope*>(scope))) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Constraints(), scope)) {
    return actual;
  } else if (const UHDM::extends* ext = scope->Extends()) {
    if (const UHDM::ref_typespec* rt = ext->Class_typespec()) {
      if (const UHDM::class_typespec* cts =
              rt->Actual_typespec<UHDM::class_typespec>()) {
        return findInClass_defn(name, object, cts->Class_defn());
      }
    }
  }

  return findInScope(name, object, scope);
}

const UHDM::any* ObjectBinder::findInModule_inst(
    std::string_view name, const UHDM::any* const object,
    const UHDM::module_inst* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if (areSimilarNames(scope, name)) {
    return scope;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Interfaces(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual = findInVectorOfAny(
                 name, object, scope->Interface_arrays(), scope)) {
    return actual;
  }

  return findInInstance(name, object, scope);
}

const UHDM::any* ObjectBinder::findInDesign(std::string_view name,
                                            const UHDM::any* const object,
                                            const UHDM::design* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if (areSimilarNames(name, "$root") || areSimilarNames(scope, name)) {
    return scope;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Parameters(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual = findInVectorOfAny(
                 name, object, scope->Param_assigns(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->AllPackages(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->AllModules(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->AllClasses(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual = findInVectorOfAny(
                 name, object, scope->AllInterfaces(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->AllPrograms(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->AllUdps(), scope)) {
    return actual;
  }

  return nullptr;
}

const UHDM::any* ObjectBinder::getPrefix(const UHDM::any* const object) {
  if (object == nullptr) return nullptr;
  if (m_prefixStack.empty()) return nullptr;
  if (m_prefixStack.back() != object->VpiParent()) return nullptr;

  const UHDM::any* const parent = object->VpiParent();
  switch (parent->UhdmType()) {
    case UHDM::uhdmhier_path: {
      const UHDM::hier_path* hp = static_cast<const UHDM::hier_path*>(parent);
      if (hp->Path_elems() && (hp->Path_elems()->size() > 1)) {
        for (size_t i = 1, n = hp->Path_elems()->size(); i < n; ++i) {
          if (hp->Path_elems()->at(i) == object) {
            const UHDM::any* const previous = hp->Path_elems()->at(i - 1);
            if (const UHDM::ref_obj* const ro1 =
                    any_cast<UHDM::ref_obj>(previous)) {
              if (areSimilarNames(ro1, "this") ||
                  areSimilarNames(ro1, "super")) {
                const UHDM::any* prefix = ro1->VpiParent();
                while (prefix != nullptr) {
                  if (prefix->UhdmType() == UHDM::uhdmclass_defn) return prefix;

                  prefix = prefix->VpiParent();
                }
                return prefix;
              }

              if (const UHDM::class_var* const cv =
                      ro1->Actual_group<UHDM::class_var>()) {
                if (const UHDM::ref_typespec* const iod2 = cv->Typespec()) {
                  return iod2->Actual_typespec();
                }
              } else if (const UHDM::io_decl* iod =
                             ro1->Actual_group<UHDM::io_decl>()) {
                if (const UHDM::ref_typespec* const iod2 = iod->Typespec()) {
                  return iod2->Actual_typespec();
                }
              } else if (const UHDM::struct_var* const sv =
                             ro1->Actual_group<UHDM::struct_var>()) {
                if (const UHDM::ref_typespec* const iod2 = sv->Typespec()) {
                  return iod2->Actual_typespec();
                }
              } else if (const UHDM::parameter* const p1 =
                             ro1->Actual_group<UHDM::parameter>()) {
                if (const UHDM::ref_typespec* const iod2 = p1->Typespec()) {
                  return iod2->Actual_typespec();
                }
              } else if (const UHDM::scope* const s =
                             ro1->Actual_group<UHDM::scope>()) {
                return s;
              } else if (const UHDM::logic_net* const ln =
                             ro1->Actual_group<UHDM::logic_net>()) {
                // Ideally logic_net::Typespec should be valid but for
                // too many (or rather most) cases, the Typespec isn't set.
                // So, use the corresponding port in the parent module to
                // find the typespec.

                const UHDM::typespec* ts = nullptr;
                if (const UHDM::ref_typespec* rt = ln->Typespec()) {
                  ts = rt->Actual_typespec();
                } else if (const UHDM::module_inst* mi =
                               ln->VpiParent<UHDM::module_inst>()) {
                  if (mi->Ports() != nullptr) {
                    for (const UHDM::port* p2 : *mi->Ports()) {
                      if (const UHDM::ref_obj* ro2 =
                              p2->Low_conn<UHDM::ref_obj>()) {
                        if (ro2->Actual_group() == ln) {
                          if (const UHDM::ref_typespec* rt = p2->Typespec()) {
                            ts = rt->Actual_typespec();
                          }
                          break;
                        }
                      }
                    }
                  }
                }

                if (const UHDM::class_typespec* const cts =
                        any_cast<UHDM::class_typespec>(ts)) {
                  return cts->Class_defn();
                } else if (const UHDM::struct_typespec* const sts =
                               any_cast<UHDM::struct_typespec>(ts)) {
                  return sts;
                }
              }
            }
            break;
          }
        }
      }
    } break;

    default:
      break;
  }

  return nullptr;
}

const UHDM::any* ObjectBinder::bindObject(const UHDM::any* const object) {
  const ValuedComponentI* component = nullptr;
  const UHDM::instance* scope = nullptr;
  const UHDM::any* parent = object->VpiParent();

  std::string_view name = object->VpiName();
  name = StringUtils::trim(name);
  if (name.empty()) return nullptr;

  m_searched.clear();
  if (name.find("::") != std::string::npos) {
    std::vector<std::string_view> tokens;
    StringUtils::tokenizeMulti(name, "::", tokens);
    if (tokens.size() > 1) {
      const std::string_view baseName = tokens[0];
      if (const UHDM::package* const p = getPackage(baseName)) {
        parent = p;
      } else if (const UHDM::class_defn* const c = getClass_defn(baseName)) {
        parent = c;
      }
    }
  } else if (!m_prefixStack.empty()) {
    if (const UHDM::any* const prefix = getPrefix(object)) {
      parent = prefix;
      std::string_view::size_type npos = name.find('.');
      if (npos != std::string_view::npos) name.remove_prefix(npos + 1);
    }
  }

  while (parent != nullptr) {
    switch (parent->UhdmType()) {
      case UHDM::uhdmfunction: {
        if (const UHDM::any* const actual = findInFunction(
                name, object, static_cast<const UHDM::function*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmtask: {
        if (const UHDM::any* const actual = findInTask(
                name, object, static_cast<const UHDM::task*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmfor_stmt: {
        if (const UHDM::any* const actual = findInFor_stmt(
                name, object, static_cast<const UHDM::for_stmt*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmforeach_stmt: {
        if (const UHDM::any* const actual = findInForeach_stmt(
                name, object, static_cast<const UHDM::foreach_stmt*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmbegin: {
        if (const UHDM::any* const actual = findInScope_sub(
                name, object, static_cast<const UHDM::begin*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmfork_stmt: {
        if (const UHDM::any* const actual = findInScope_sub(
                name, object, static_cast<const UHDM::fork_stmt*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmclass_defn: {
        if (const UHDM::any* const actual = findInClass_defn(
                name, object, static_cast<const UHDM::class_defn*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmmodule_inst: {
        if (const UHDM::any* const actual = findInModule_inst(
                name, object, static_cast<const UHDM::module_inst*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdminterface_inst: {
        if (const UHDM::any* const actual = findInInterface_inst(
                name, object,
                static_cast<const UHDM::interface_inst*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmprogram: {
        if (const UHDM::any* const actual = findInProgram(
                name, object, static_cast<const UHDM::program*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmpackage: {
        if (const UHDM::any* const actual = findInPackage(
                name, object, static_cast<const UHDM::package*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmudp_defn: {
        if (const UHDM::any* const actual = findInUdp_defn(
                name, object, static_cast<const UHDM::udp_defn*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmdesign: {
        if (const UHDM::any* const actual = findInDesign(
                name, object, static_cast<const UHDM::design*>(parent))) {
          return actual;
        }
      } break;

      default: {
        if (const UHDM::typespec* ts = any_cast<UHDM::typespec>(parent)) {
          if (const UHDM::any* const actual =
                  findInTypespec(name, object, ts)) {
            return actual;
          }
        }
      } break;
    }

    BaseClassMap::const_iterator it = m_baseclassMap.find(parent);
    if (it != m_baseclassMap.end()) {
      component = it->second;
      scope = any_cast<UHDM::instance>(parent);
    }

    parent = parent->VpiParent();
  }

  for (ComponentMap::const_reference entry : m_componentMap) {
    const DesignComponent* dc =
        valuedcomponenti_cast<DesignComponent>(entry.first);
    if (dc == nullptr) continue;

    const auto& fileContents = dc->getFileContents();
    if (!fileContents.empty()) {
      if (const FileContent* const fC = fileContents.front()) {
        for (const auto& td : fC->getTypeDefMap()) {
          const DataType* dt = td.second;
          while (dt != nullptr) {
            const UHDM::typespec* const ts = dt->getTypespec();
            if (const UHDM::any* const actual =
                    findInTypespec(name, object, ts)) {
              return actual;
            }
            dt = dt->getDefinition();
          }
        }
      }
    }
  }

  m_unbounded.emplace_back(object, scope, component);
  return nullptr;
}

void ObjectBinder::enterBit_select(const UHDM::bit_select* const object) {
  if (object->Actual_group() != nullptr) return;

  if (const UHDM::any* actual = bindObject(object)) {
    const_cast<UHDM::bit_select*>(object)->Actual_group(
        const_cast<UHDM::any*>(actual));
  }
}

// void ObjectBinder::enterChandle_var(const UHDM::chandle_var* const object) {
//   if (object->Actual_group() != nullptr) return;
//
//   if (const UHDM::any* actual = bindObject(object)) {
//     const_cast<UHDM::chandle_var*>(object)->Actual_group(
//         const_cast<UHDM::any*>(actual));
//   }
// }

void ObjectBinder::enterIndexed_part_select(
    const UHDM::indexed_part_select* const object) {
  if (object->Actual_group() != nullptr) return;

  if (const UHDM::any* actual = bindObject(object)) {
    const_cast<UHDM::indexed_part_select*>(object)->Actual_group(
        const_cast<UHDM::any*>(actual));
  }
}

void ObjectBinder::enterPart_select(const UHDM::part_select* const object) {
  if (object->Actual_group() != nullptr) return;

  if (const UHDM::any* actual = bindObject(object)) {
    const_cast<UHDM::part_select*>(object)->Actual_group(
        const_cast<UHDM::any*>(actual));
  }
}

void ObjectBinder::enterVar_select(const UHDM::var_select* const object) {
  if (object->Actual_group() != nullptr) return;

  if (const UHDM::any* actual = bindObject(object)) {
    const_cast<UHDM::var_select*>(object)->Actual_group(
        const_cast<UHDM::any*>(actual));
  }
}

void ObjectBinder::enterRef_module(const UHDM::ref_module* const object) {
  if (object->Actual_instance() != nullptr) return;

  if (const UHDM::any* const actual = getModuleInst(object->VpiDefName())) {
    const_cast<UHDM::ref_module*>(object)->Actual_instance(
        const_cast<UHDM::instance*>(any_cast<UHDM::instance>(actual)));
  } else if (const UHDM::any* const actual =
                 getInterfaceInst(object->VpiDefName())) {
    const_cast<UHDM::ref_module*>(object)->Actual_instance(
        const_cast<UHDM::instance*>(any_cast<UHDM::instance>(actual)));
  }
}

void ObjectBinder::enterRef_obj(const UHDM::ref_obj* const object) {
  if (object->Actual_group() != nullptr) return;

  if (const UHDM::any* actual = bindObject(object)) {
    // Reporting error for $root.
    if ((actual->UhdmType() == UHDM::uhdmdesign) &&
        areSimilarNames(object, "$root"))
      return;

    const_cast<UHDM::ref_obj*>(object)->Actual_group(
        const_cast<UHDM::any*>(actual));
  }
}

void ObjectBinder::enterRef_typespec(const UHDM::ref_typespec* const object) {
  const UHDM::typespec* const object_Actual_typespec =
      object->Actual_typespec();
  if ((object_Actual_typespec != nullptr) &&
      (object_Actual_typespec->UhdmType() == UHDM::uhdmunsupported_typespec)) {
    const_cast<UHDM::ref_typespec*>(object)->Actual_typespec(nullptr);
  }

  if (object->Actual_typespec() != nullptr) return;

  if (const UHDM::any* actual = bindObject(object)) {
    const_cast<UHDM::ref_typespec*>(object)->Actual_typespec(
        const_cast<UHDM::typespec*>(any_cast<UHDM::typespec>(actual)));
  }

  if ((object_Actual_typespec != nullptr) &&
      (object->Actual_typespec() == nullptr)) {
    const_cast<UHDM::ref_typespec*>(object)->Actual_typespec(
        const_cast<UHDM::typespec*>(object_Actual_typespec));
  }
}

void ObjectBinder::enterClass_defn(const UHDM::class_defn* const object) {
  const UHDM::extends* extends = object->Extends();
  if (extends == nullptr) return;
  if (extends->Class_typespec() == nullptr) return;
  if (extends->Class_typespec()->Actual_typespec() != nullptr) return;

  UHDM::ref_typespec* rt =
      const_cast<UHDM::ref_typespec*>(extends->Class_typespec());
  if (rt == nullptr) return;

  UHDM::class_defn* derClsDef = const_cast<UHDM::class_defn*>(object);
  for (ComponentMap::const_reference entry : m_componentMap) {
    const DesignComponent* dc =
        valuedcomponenti_cast<DesignComponent>(entry.first);
    if (dc == nullptr) continue;
    if (!areSimilarNames(rt, dc->getName())) continue;

    const ClassDefinition* bdef =
        valuedcomponenti_cast<ClassDefinition>(entry.first);

    if (bdef != nullptr) {
      // const DataType* the_def = bdef->getDataType(rt->VpiName());
      // Property* prop = new Property(
      //     the_def, bdef->getFileContent(), bdef->getNodeId(),
      //     InvalidNodeId, "super",
      //                  false, false, false, false, false);
      // bdef->insertProperty(prop);

      if (UHDM::class_defn* base = bdef->getUhdmModel<UHDM::class_defn>()) {
        UHDM::Serializer& s = *base->GetSerializer();
        UHDM::class_typespec* tps = s.MakeClass_typespec();
        tps->VpiLineNo(rt->VpiLineNo());
        tps->VpiColumnNo(rt->VpiColumnNo());
        tps->VpiEndLineNo(rt->VpiEndLineNo());
        tps->VpiEndColumnNo(rt->VpiEndColumnNo());
        tps->VpiName(rt->VpiName());
        tps->VpiFile(base->VpiFile());
        tps->VpiParent(derClsDef);
        rt->Actual_typespec(tps);

        tps->Class_defn(base);
        base->Deriveds(true)->emplace_back(derClsDef);
      }
      break;
    }
  }
}

void ObjectBinder::reportErrors() {
  SymbolTable* const symbolTable = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  ErrorContainer* const errorContainer = m_session->getErrorContainer();

  for (auto& [object, scope, component] : m_unbounded) {
    bool reportMissingActual = false;
    if (const UHDM::ref_obj* const ro = any_cast<UHDM::ref_obj>(object)) {
      if (ro->Actual_group() == nullptr) {
        if (const UHDM::any* const parent = object->VpiParent()) {
          if (!((parent->UhdmType() == UHDM::uhdmhier_path) &&
                (areSimilarNames(object, "size") ||
                 areSimilarNames(object, "delete"))))
            reportMissingActual = true;
          if (!areSimilarNames(object, "default")) reportMissingActual = true;
        }
      }
    } else if (const UHDM::ref_typespec* const rt =
                   any_cast<UHDM::ref_typespec>(object)) {
      if ((rt->Actual_typespec() == nullptr) ||
          (rt->Actual_typespec()->UhdmType() ==
           UHDM::uhdmunsupported_typespec)) {
        reportMissingActual = true;
      }
    } else if (const UHDM::ref_module* const rm =
                   any_cast<UHDM::ref_module>(object)) {
      if (rm->Actual_instance() == nullptr) {
        reportMissingActual = true;
      }
    }

    if (reportMissingActual) {
      const std::string text =
          StrCat("id: ", object->UhdmId(), ", name: ", object->VpiName());
      Location loc(fileSystem->toPathId(object->VpiFile(), symbolTable),
                   object->VpiLineNo(), object->VpiColumnNo(),
                   symbolTable->registerSymbol(text));
      Error err(ErrorDefinition::UHDM_FAILED_TO_BIND, loc);
      errorContainer->addError(err);
      errorContainer->printMessages(m_muteStdout);
    }

    if (getDefaultNetType(component) == VObjectType::slNoType) {
      const std::string text =
          StrCat("id:", object->UhdmId(), ", name: ", object->VpiName());
      Location loc(fileSystem->toPathId(object->VpiFile(), symbolTable),
                   object->VpiLineNo(), object->VpiColumnNo(),
                   symbolTable->registerSymbol(text));
      Error err(ErrorDefinition::ELAB_ILLEGAL_IMPLICIT_NET, loc);
      errorContainer->addError(err);
      errorContainer->printMessages(m_muteStdout);
    }
  }
}

bool ObjectBinder::createDefaultNets() {
  bool result = false;
  Unbounded unbounded(m_unbounded);
  m_unbounded.clear();
  for (Unbounded::const_reference entry : unbounded) {
    const UHDM::any* const object = std::get<0>(entry);
    const ValuedComponentI* const component = std::get<2>(entry);

    const UHDM::ref_obj* ro = any_cast<UHDM::ref_obj>(object);
    if (ro == nullptr) continue;

    if (ro->Actual_group() == nullptr) {
      enterRef_obj(ro);
    }

    if (ro->Actual_group() != nullptr) continue;

    const UHDM::any* const pro = ro->VpiParent();
    if ((pro != nullptr) && (pro->UhdmType() == UHDM::uhdmsys_func_call) &&
        areSimilarNames(pro, "$bits")) {
      UHDM::ref_typespec* const rt = m_serializer.MakeRef_typespec();
      rt->VpiName(object->VpiName());
      rt->VpiParent(const_cast<UHDM::any*>(object->VpiParent()));
      enterRef_typespec(rt);

      if (rt->Actual_typespec() == nullptr) {
        rt->VpiParent(nullptr);
        m_unbounded.erase(std::find_if(m_unbounded.begin(), m_unbounded.end(),
                                       [rt](Unbounded::value_type& entry) {
                                         return std::get<0>(entry) == rt;
                                       }));
        m_serializer.Erase(rt);
      } else {
        rt->VpiFile(ro->VpiFile());
        rt->VpiLineNo(ro->VpiLineNo());
        rt->VpiColumnNo(ro->VpiColumnNo());
        rt->VpiEndLineNo(ro->VpiEndLineNo());
        rt->VpiEndColumnNo(ro->VpiEndColumnNo());
        UHDM::VectorOfany* const args =
            static_cast<const UHDM::sys_func_call*>(object->VpiParent())
                ->Tf_call_args();
        *std::find(args->begin(), args->end(), object) = rt;
        m_unbounded.erase(std::find_if(m_unbounded.begin(), m_unbounded.end(),
                                       [object](Unbounded::value_type& entry) {
                                         return std::get<0>(entry) == object;
                                       }));
        continue;
      }
    }

    VObjectType defaultNetType = getDefaultNetType(component);
    if (defaultNetType != VObjectType::slNoType) {
      const UHDM::any* parent = object->VpiParent();
      while ((parent != nullptr) && (parent->Cast<UHDM::scope>() == nullptr)) {
        parent = parent->VpiParent();
      }
      UHDM::logic_net* net = m_serializer.MakeLogic_net();
      net->VpiName(object->VpiName());
      net->VpiParent(const_cast<UHDM::any*>(parent));
      net->VpiNetType(UhdmWriter::getVpiNetType(defaultNetType));
      net->VpiFile(object->VpiFile());
      net->VpiLineNo(object->VpiLineNo());
      net->VpiColumnNo(object->VpiColumnNo());
      net->VpiEndLineNo(object->VpiEndLineNo());
      net->VpiEndColumnNo(object->VpiEndColumnNo());
      const_cast<UHDM::ref_obj*>(ro)->Actual_group(net);
      result = true;
    }
  }
  return result;
}

void ObjectBinder::bind(const UHDM::design* const object, bool report) {
  listenDesign(object);
  while (createDefaultNets()) {
    // Nothing to do here!
  }
  if (report) reportErrors();
}

void ObjectBinder::bind(const std::vector<const UHDM::design*>& objects,
                        bool report) {
  for (const UHDM::design* d : objects) {
    bind(d, report);
  }
}
}  // namespace SURELOG
