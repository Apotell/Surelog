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
#include <uhdm/Utils.h>
#include <uhdm/uhdm.h>

namespace SURELOG {
ObjectBinder::ObjectBinder(Session* session,
                           const ForwardComponentMap& componentMap,
                           uhdm::Serializer& serializer, bool muteStdout)
    : m_session(session),
      m_forwardComponentMap(componentMap),
      m_serializer(serializer),
      m_muteStdout(muteStdout) {
  for (ForwardComponentMap::const_reference entry : m_forwardComponentMap) {
    m_reverseComponentMap.emplace(entry.second, entry.first);
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

inline bool ObjectBinder::areSimilarNames(const uhdm::Any* object1,
                                          std::string_view name2) const {
  return areSimilarNames(object1->getName(), name2);
}

inline bool ObjectBinder::areSimilarNames(const uhdm::Any* object1,
                                          const uhdm::Any* object2) const {
  return areSimilarNames(object1->getName(), object2->getName());
}

bool ObjectBinder::isInElaboratedTree(const uhdm::Any* object) {
  const uhdm::Any* p = object;
  while (p != nullptr) {
    if (const uhdm::Instance* const inst = any_cast<uhdm::Instance>(p)) {
      if (inst->getTop()) return true;
    }
    p = p->getParent();
  }
  return false;
}

VObjectType ObjectBinder::getDefaultNetType(const uhdm::Any* object) const {
  const ValuedComponentI* component = nullptr;
  while ((object != nullptr) && (component == nullptr)) {
    ReverseComponentMap::const_iterator it = m_reverseComponentMap.find(object);
    if (it != m_reverseComponentMap.end()) {
      component = it->second;
    }

    object = object->getParent();
  }

  if (component == nullptr) return VObjectType::NO_TYPE;

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

  return VObjectType::NO_TYPE;
}

const uhdm::Package* ObjectBinder::getPackage(std::string_view name,
                                              const uhdm::Any* object) const {
  if (const uhdm::Package* const p = uhdm::getParent<uhdm::Package>(object)) {
    if (areSimilarNames(p, name)) {
      return p;
    }
  }

  if (const uhdm::Design* const d = uhdm::getParent<uhdm::Design>(object)) {
    if (const uhdm::PackageCollection* const packages = d->getAllPackages()) {
      for (const uhdm::Package* p : *packages) {
        if (areSimilarNames(p, name)) {
          return p;
        }
      }
    }
  }

  return nullptr;
}

const uhdm::Module* ObjectBinder::getModule(std::string_view defname,
                                            const uhdm::Any* object) const {
  if (const uhdm::Module* const m = uhdm::getParent<uhdm::Module>(object)) {
    if (m->getDefName() == defname) {
      return m;
    }
  }

  if (const uhdm::Design* const d = uhdm::getParent<uhdm::Design>(object)) {
    if (const uhdm::ModuleCollection* const modules = d->getAllModules()) {
      for (const uhdm::Module* m : *modules) {
        if (m->getDefName() == defname) {
          return m;
        }
      }
    }
  }

  return nullptr;
}

const uhdm::Interface* ObjectBinder::getInterface(
    std::string_view defname, const uhdm::Any* object) const {
  if (const uhdm::Interface* const i =
          uhdm::getParent<uhdm::Interface>(object)) {
    if (i->getDefName() == defname) {
      return i;
    }
  }

  if (const uhdm::Design* d = uhdm::getParent<uhdm::Design>(object)) {
    if (const uhdm::InterfaceCollection* const interfaces =
            d->getAllInterfaces()) {
      for (const uhdm::Interface* i : *interfaces) {
        if (i->getDefName() == defname) {
          return i;
        }
      }
    }
  }

  return nullptr;
}

const uhdm::ClassDefn* ObjectBinder::getClassDefn(
    const uhdm::ClassDefnCollection* collection, std::string_view name) {
  if (collection != nullptr) {
    for (const uhdm::ClassDefn* c : *collection) {
      if (areSimilarNames(c, name)) {
        return c;
      }
    }
  }
  return nullptr;
}

const uhdm::ClassDefn* ObjectBinder::getClassDefn(std::string_view name,
                                                  const uhdm::Any* object) {
  if (const uhdm::ClassDefn* const c =
          uhdm::getParent<uhdm::ClassDefn>(object)) {
    if (areSimilarNames(c, name)) {
      return c;
    }
  }

  if (const uhdm::Package* const p = uhdm::getParent<uhdm::Package>(object)) {
    if (const uhdm::ClassDefn* const c =
            getClassDefn(p->getClassDefns(), name)) {
      return c;
    }
  }

  if (const uhdm::Design* const d = uhdm::getParent<uhdm::Design>(object)) {
    if (const uhdm::ClassDefn* const c =
            getClassDefn(d->getAllClasses(), name)) {
      return c;
    } else if (const uhdm::Any* const actual = findInCollection(
                   name, RefType::Object, d->getTypespecs(), d)) {
      return any_cast<uhdm::ClassDefn>(actual);
    }
  }

  return nullptr;
}

const uhdm::Any* ObjectBinder::findInTypespec(std::string_view name,
                                              RefType refType,
                                              const uhdm::Typespec* scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  switch (scope->getUhdmType()) {
    case uhdm::UhdmType::EnumTypespec: {
      if (const uhdm::Any* const actual = findInCollection(
              name, refType,
              static_cast<const uhdm::EnumTypespec*>(scope)->getEnumConsts(),
              scope)) {
        return actual;
      }
    } break;

    case uhdm::UhdmType::StructTypespec: {
      if (const uhdm::Any* const actual = findInCollection(
              name, refType,
              static_cast<const uhdm::StructTypespec*>(scope)->getMembers(),
              scope)) {
        return actual;
      }
    } break;

    case uhdm::UhdmType::UnionTypespec: {
      if (const uhdm::Any* const actual = findInCollection(
              name, refType,
              static_cast<const uhdm::UnionTypespec*>(scope)->getMembers(),
              scope)) {
        return actual;
      }
    } break;

    case uhdm::UhdmType::ImportTypespec: {
      const uhdm::ImportTypespec* const it =
          any_cast<uhdm::ImportTypespec>(scope);
      if (const uhdm::Constant* const i = it->getItem()) {
        if ((i->getValue() == "STRING:*") ||
            (i->getValue() == StrCat("STRING:", name))) {
          if (const uhdm::Package* const p = getPackage(it->getName(), it)) {
            if (const uhdm::Any* const actual =
                    findInPackage(name, refType, p)) {
              return actual;
            }
          }
        }
      }
    } break;

    case uhdm::UhdmType::ClassTypespec: {
      if (const uhdm::ClassDefn* cd =
              static_cast<const uhdm::ClassTypespec*>(scope)->getClassDefn()) {
        if (const uhdm::Any* const actual =
                findInClassDefn(name, refType, cd)) {
          return actual;
        }
      }
    } break;

    case uhdm::UhdmType::InterfaceTypespec: {
      if (const uhdm::Interface* ins =
              static_cast<const uhdm::InterfaceTypespec*>(scope)
                  ->getInterface()) {
        if (const uhdm::Any* const actual =
                findInInterface(name, refType, ins)) {
          return actual;
        }
      }
    } break;

    case uhdm::UhdmType::TypedefTypespec: {
      if (const uhdm::Any* const actual =
              findInRefTypespec(name, refType,
                                static_cast<const uhdm::TypedefTypespec*>(scope)
                                    ->getTypedefAlias())) {
        return actual;
      }
    } break;

    case uhdm::UhdmType::ArrayTypespec: {
      if (const uhdm::Any* const actual =
              findInRefTypespec(name, refType,
                                static_cast<const uhdm::ArrayTypespec*>(scope)
                                    ->getElemTypespec())) {
        return actual;
      }
    } break;

    default:
      break;
  }

  return nullptr;
}

const uhdm::Any* ObjectBinder::findInRefTypespec(
    std::string_view name, RefType refType, const uhdm::RefTypespec* scope) {
  if (scope == nullptr) return nullptr;

  if (const uhdm::Typespec* const ts = scope->getActual()) {
    return findInTypespec(name, refType, ts);
  }
  return nullptr;
}

template <typename T>
const uhdm::Any* ObjectBinder::findInCollection(
    std::string_view name, RefType refType, const std::vector<T*>* collection,
    const uhdm::Any* scope) {
  if (collection == nullptr) return nullptr;

  std::string_view shortName = name;
  if (shortName.find("::") != std::string::npos) {
    std::vector<std::string_view> tokens;
    StringUtils::tokenizeMulti(shortName, "::", tokens);
    if (tokens.size() > 1) shortName = tokens.back();
  }

  for (const uhdm::Any* c : *collection) {
    if (c->getUhdmType() == uhdm::UhdmType::UnsupportedTypespec) continue;
    if (c->getUhdmType() == uhdm::UhdmType::UnsupportedStmt) continue;
    if (c->getUhdmType() == uhdm::UhdmType::UnsupportedExpr) continue;
    if (c->getUhdmType() == uhdm::UhdmType::VarSelect) continue;
    if (c == scope) continue;
    if (m_searched.find(c) != m_searched.cend()) continue;

    if (any_cast<uhdm::Typespec>(c) == nullptr) {
      if ((refType == RefType::Object) &&
          (any_cast<uhdm::RefObj>(c) == nullptr)) {
        if (areSimilarNames(c, name)) return c;
        if (areSimilarNames(c, shortName)) return c;
      }
    } else {
      if ((refType == RefType::Typespec) &&
          (any_cast<uhdm::RefTypespec>(c) == nullptr)) {
        if (areSimilarNames(c, name)) return c;
        if (areSimilarNames(c, shortName)) return c;
      }
    }

    if (const uhdm::EnumTypespec* const et = any_cast<uhdm::EnumTypespec>(c)) {
      if (const uhdm::Any* const actual = findInTypespec(name, refType, et)) {
        return actual;
      }
    } else if (const uhdm::ImportTypespec* const it =
                   any_cast<uhdm::ImportTypespec>(c)) {
      if (const uhdm::Any* const actual = findInTypespec(name, refType, it)) {
        return actual;
      }
    }

    if (const uhdm::Variable* const v = any_cast<uhdm::Variable>(c)) {
      if (const uhdm::RefTypespec* const rt = v->getTypespec()) {
        if (rt->getActual<uhdm::EnumTypespec>() != nullptr) {
          if (const uhdm::Any* const actual =
                  findInRefTypespec(name, refType, rt)) {
            return actual;
          } else if (const uhdm::Any* const actual =
                         findInRefTypespec(shortName, refType, rt)) {
            return actual;
          }
        }
      }
    }
    // if (c->getUhdmType() == uhdm::UhdmType::StructVar) {
    //   if (const uhdm::Any* const actual = findInRefTypespec(
    //           name, static_cast<const uhdm::StructVar*>(c)->getTypespec())) {
    //     return actual;
    //   }
    // }
    if (const uhdm::RefTypespec* rt = any_cast<uhdm::RefTypespec>(c)) {
      if (scope != rt->getActual()) {
        if (const uhdm::Any* const actual =
                findInRefTypespec(name, refType, rt)) {
          return actual;
        } else if (const uhdm::Any* const actual =
                       findInRefTypespec(shortName, refType, rt)) {
          return actual;
        }
      }
    }
  }

  return nullptr;
}

const uhdm::Any* ObjectBinder::findInScope(std::string_view name,
                                           RefType refType,
                                           const uhdm::Scope* scope) {
  if (scope == nullptr) return nullptr;

  if ((refType == RefType::Object) && areSimilarNames(scope, name)) {
    return scope;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getVariables(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getParamAssigns(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getParameters(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getPropertyDecls(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getTypespecs(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getNamedEvents(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getInternalScopes(), scope)) {
    return actual;
  } else if (const uhdm::Package* const p = any_cast<uhdm::Package>(scope)) {
    std::string fullName = StrCat(p->getName(), "::", name);
    if (const uhdm::Any* const actual =
            findInCollection(fullName, refType, scope->getTypespecs(), scope)) {
      return actual;
    }
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getInstanceItems(), scope)) {
    return actual;
  }

  return nullptr;
}

const uhdm::Any* ObjectBinder::findInInstance(std::string_view name,
                                              RefType refType,
                                              const uhdm::Instance* scope) {
  if (scope == nullptr) return nullptr;

  if (const uhdm::Any* const actual =
          findInCollection(name, refType, scope->getNets(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getTaskFuncs(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual =
                 findInCollection(name, refType, scope->getPrograms(), scope)) {
    return actual;
  }

  return findInScope(name, refType, scope);
}

const uhdm::Any* ObjectBinder::findInInterface(std::string_view name,
                                               RefType refType,
                                               const uhdm::Interface* scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if ((refType == RefType::Object) && areSimilarNames(scope, name)) {
    return scope;
  } else if (const uhdm::Any* const actual =
                 findInCollection(name, refType, scope->getModports(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getInterfaceTFDecls(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual =
                 findInCollection(name, refType, scope->getPorts(), scope)) {
    return actual;
  }
  return findInInstance(name, refType, scope);
}

const uhdm::Any* ObjectBinder::findInPackage(std::string_view name,
                                             RefType refType,
                                             const uhdm::Package* scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if ((refType == RefType::Object) && areSimilarNames(scope, name)) {
    return scope;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getParameters(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual =
                 findInInstance(name, refType, scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getTypespecs(), scope)) {
    return actual;
  }

  return nullptr;
}

const uhdm::Any* ObjectBinder::findInUdpDefn(std::string_view name,
                                             RefType refType,
                                             const uhdm::UdpDefn* scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if ((refType == RefType::Object) && areSimilarNames(scope, name)) {
    return scope;
  }

  return findInCollection(name, refType, scope->getIODecls(), scope);
}

const uhdm::Any* ObjectBinder::findInProgram(std::string_view name,
                                             RefType refType,
                                             const uhdm::Program* scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if ((refType == RefType::Object) && areSimilarNames(scope, name)) {
    return scope;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getParameters(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual =
                 findInCollection(name, refType, scope->getPorts(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getInterfaces(), scope)) {
    return actual;
  }

  return findInInstance(name, refType, scope);
}

const uhdm::Any* ObjectBinder::findInFunction(std::string_view name,
                                              RefType refType,
                                              const uhdm::Function* scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if ((refType == RefType::Object) && areSimilarNames(scope, name)) {
    return scope;
  } else if (const uhdm::Any* const actual =
                 findInCollection(name, refType, scope->getIODecls(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getVariables(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getParameters(), scope)) {
    return actual;
    // } else if (const uhdm::Package* const inst =
    //               scope->getInstance<uhdm::Package>()) {
    //  if (const uhdm::Any* const actual = findInPackage(name, refType, inst))
    //  {
    //    return actual;
    //  }
  }

  return findInScope(name, refType, scope);
}

const uhdm::Any* ObjectBinder::findInTask(std::string_view name,
                                          RefType refType,
                                          const uhdm::Task* scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if ((refType == RefType::Object) && areSimilarNames(scope, name)) {
    return scope;
  } else if (const uhdm::Any* const actual =
                 findInCollection(name, refType, scope->getIODecls(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getVariables(), scope)) {
    return actual;
  } else if (const uhdm::Package* const p =
                 scope->getInstance<uhdm::Package>()) {
    if (const uhdm::Any* const actual = findInPackage(name, refType, p)) {
      return actual;
    }
  }

  return findInScope(name, refType, scope);
}

const uhdm::Any* ObjectBinder::findInForStmt(std::string_view name,
                                             RefType refType,
                                             const uhdm::ForStmt* scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if (refType == RefType::Object) {
    std::string_view shortName = name;
    if (shortName.find("::") != std::string::npos) {
      std::vector<std::string_view> tokens;
      StringUtils::tokenizeMulti(shortName, "::", tokens);
      if (tokens.size() > 1) shortName = tokens.back();
    }

    if (const uhdm::AnyCollection* const inits = scope->getForInitStmts()) {
      for (auto init : *inits) {
        if (init->getUhdmType() == uhdm::UhdmType::AssignStmt) {
          const uhdm::Expr* const lhs =
              static_cast<const uhdm::AssignStmt*>(init)->getLhs();
          if (lhs->getUhdmType() == uhdm::UhdmType::UnsupportedTypespec)
            continue;
          if (lhs->getUhdmType() == uhdm::UhdmType::UnsupportedStmt) continue;
          if (lhs->getUhdmType() == uhdm::UhdmType::UnsupportedExpr) continue;
          if (lhs->getUhdmType() == uhdm::UhdmType::VarSelect) continue;
          if (any_cast<uhdm::RefObj>(lhs) != nullptr) continue;
          if (static_cast<const uhdm::Any*>(lhs) ==
              static_cast<const uhdm::Any*>(scope))
            continue;
          if (m_searched.find(lhs) != m_searched.cend()) continue;
          if (areSimilarNames(lhs, name)) return lhs;
          if (areSimilarNames(lhs, shortName)) return lhs;
        }
      }
    }
  }

  return findInScope(name, refType, scope);
}

const uhdm::Any* ObjectBinder::findInForeachStmt(
    std::string_view name, RefType refType, const uhdm::ForeachStmt* scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if (const uhdm::Any* const var =
          findInCollection(name, refType, scope->getLoopVars(), scope)) {
    return var;
  }

  return findInScope(name, refType, scope);
}

template <typename T>
const uhdm::Any* ObjectBinder::findInScope_sub(
    std::string_view name, RefType refType, const T* scope,
    typename std::enable_if<
        std::is_same<uhdm::Begin, typename std::decay<T>::type>::value ||
        std::is_same<uhdm::ForkStmt,
                     typename std::decay<T>::type>::value>::type* /* = 0 */) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  std::string_view shortName = name;
  if (refType == RefType::Object) {
    if (shortName.find("::") != std::string::npos) {
      std::vector<std::string_view> tokens;
      StringUtils::tokenizeMulti(shortName, "::", tokens);
      if (tokens.size() > 1) shortName = tokens.back();
    }

    if (areSimilarNames(scope, name) || areSimilarNames(scope, shortName)) {
      return scope;
    }
  }

  if (const uhdm::Any* const actual =
          findInCollection(name, refType, scope->getVariables(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getParameters(), scope)) {
    return actual;
  }

  if (refType == RefType::Object) {
    if (const uhdm::AnyCollection* const stmts = scope->getStmts()) {
      for (auto init : *stmts) {
        if (init->getUhdmType() == uhdm::UhdmType::AssignStmt) {
          const uhdm::Expr* const lhs =
              static_cast<const uhdm::AssignStmt*>(init)->getLhs();
          if (lhs->getUhdmType() == uhdm::UhdmType::UnsupportedTypespec)
            continue;
          if (lhs->getUhdmType() == uhdm::UhdmType::UnsupportedStmt) continue;
          if (lhs->getUhdmType() == uhdm::UhdmType::UnsupportedExpr) continue;
          if (lhs->getUhdmType() == uhdm::UhdmType::VarSelect) continue;
          if (any_cast<uhdm::RefObj>(lhs) != nullptr) continue;
          if (static_cast<const uhdm::Any*>(lhs) ==
              static_cast<const uhdm::Any*>(scope))
            continue;
          if (m_searched.find(lhs) != m_searched.cend()) continue;
          if (areSimilarNames(lhs, name)) return lhs;
          if (areSimilarNames(lhs, shortName)) return lhs;
        }
      }
    }
  }

  return findInScope(name, refType, scope);
}

const uhdm::Any* ObjectBinder::findInClassDefn(std::string_view name,
                                               RefType refType,
                                               const uhdm::ClassDefn* scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if (refType == RefType::Object) {
    std::string_view shortName = name;
    if (shortName.find("::") != std::string::npos) {
      std::vector<std::string_view> tokens;
      StringUtils::tokenizeMulti(shortName, "::", tokens);
      if (tokens.size() > 1) shortName = tokens.back();
    }

    if (areSimilarNames(name, "this")) {
      return scope;
    } else if (areSimilarNames(name, "super")) {
      if (const uhdm::Extends* ext = scope->getExtends()) {
        if (const uhdm::RefTypespec* rt = ext->getClassTypespec()) {
          if (const uhdm::ClassTypespec* cts =
                  rt->getActual<uhdm::ClassTypespec>())
            return cts->getClassDefn();
        }
      }
      return nullptr;
    }

    if (areSimilarNames(scope, name) || areSimilarNames(scope, shortName)) {
      return scope;
    }
  }

  if (const uhdm::Any* const actual =
          findInCollection(name, refType, scope->getVariables(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual =
                 findInCollection(name, refType, scope->getMethods(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInScope(
                 name, refType, static_cast<const uhdm::Scope*>(scope))) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getConstraints(), scope)) {
    return actual;
  } else if (const uhdm::Extends* const ext = scope->getExtends()) {
    if (const uhdm::RefTypespec* const rt = ext->getClassTypespec()) {
      if (const uhdm::ClassTypespec* const cts =
              rt->getActual<uhdm::ClassTypespec>()) {
        return findInClassDefn(name, refType, cts->getClassDefn());
      } else if (const uhdm::TypeParameter* const tp =
                     rt->getActual<uhdm::TypeParameter>()) {
        return findInRefTypespec(name, refType, tp->getTypespec());
      }
    }
  }

  return nullptr;
}

const uhdm::Any* ObjectBinder::findInModule(std::string_view name,
                                            RefType refType,
                                            const uhdm::Module* scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if ((refType == RefType::Object) && areSimilarNames(scope, name)) {
    return scope;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getInterfaces(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getInterfaceArrays(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getRefModules(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual =
                 findInInstance(name, refType, scope)) {
    return actual;
  } else if (const uhdm::Any* const actual =
                 findInCollection(name, refType, scope->getPorts(), scope)) {
    return actual;
  }

  return nullptr;
}

const uhdm::Any* ObjectBinder::findInDesign(std::string_view name,
                                            RefType refType,
                                            const uhdm::Design* scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  if (refType == RefType::Object) {
    if (areSimilarNames(name, "$root") || areSimilarNames(scope, name)) {
      return scope;
    }
  }

  if (const uhdm::Any* const actual =
          findInCollection(name, refType, scope->getParameters(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getVariables(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getParamAssigns(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getAllPackages(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getAllModules(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getAllClasses(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getAllInterfaces(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getAllPrograms(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual =
                 findInCollection(name, refType, scope->getAllUdps(), scope)) {
    return actual;
  } else if (const uhdm::Any* const actual = findInCollection(
                 name, refType, scope->getTypespecs(), scope)) {
    return actual;
  }
  if (refType == RefType::Typespec) {
    if (const uhdm::Any* const actual =
            findInCollection(name, refType, scope->getTypespecs(), scope)) {
      return actual;
    }
  }

  return nullptr;
}

void ObjectBinder::visitBitSelect(const uhdm::BitSelect* object) {
  if (object->getActual() != nullptr) return;

  if (const uhdm::Any* actual1 = findObject(object)) {
    const_cast<uhdm::BitSelect*>(object)->setActual(
        const_cast<uhdm::Any*>(actual1), true);

    // Typespec for BitSelect is the same as the typespec for its actual
    if (const uhdm::Typespec* const t = uhdm::getTypespec(actual1)) {
      if (any_cast<uhdm::UnsupportedTypespec>(t) == nullptr) {
        if (const uhdm::RefTypespec* const rt = object->getTypespec()) {
          if (rt->getActual<uhdm::UnsupportedTypespec>() != nullptr) {
            const_cast<uhdm::RefTypespec*>(rt)->setActual(
                const_cast<uhdm::Typespec*>(t), true);
          }
        }
      }
    }
  }
}

void ObjectBinder::visitClassDefn(const uhdm::ClassDefn* object) {
  const uhdm::Extends* extends = object->getExtends();
  if (extends == nullptr) return;
  if (extends->getClassTypespec() == nullptr) return;
  if (extends->getClassTypespec()->getActual() != nullptr) return;

  uhdm::RefTypespec* rt =
      const_cast<uhdm::RefTypespec*>(extends->getClassTypespec());
  if (rt == nullptr) return;

  uhdm::ClassDefn* derClsDef = const_cast<uhdm::ClassDefn*>(object);
  for (ForwardComponentMap::const_reference entry : m_forwardComponentMap) {
    const DesignComponent* dc =
        valuedcomponenti_cast<DesignComponent>(entry.first);
    if (dc == nullptr) continue;
    if (!areSimilarNames(rt, dc->getName())) continue;

    const ClassDefinition* bdef =
        valuedcomponenti_cast<ClassDefinition>(entry.first);

    if (bdef != nullptr) {
      // const DataType* the_def = bdef->getDataType(rt->getName());
      // Property* prop = new Property(
      //     the_def, bdef->getFileContent(), bdef->getNodeId(),
      //     InvalidNodeId, "super",
      //                  false, false, false, false, false);
      // bdef->insertProperty(prop);

      if (uhdm::ClassDefn* const base = bdef->getUhdmModel<uhdm::ClassDefn>()) {
        rt->setActual(
            const_cast<ClassDefinition*>(bdef)->getUhdmTypespecModel(), true);
        base->getDerivedClasses(true)->emplace_back(derClsDef);
      }
      break;
    }
  }
}

void ObjectBinder::visitForeachStmt(const uhdm::ForeachStmt* object) {
  // const uhdm::Any* contextAny = nullptr;
  // if (const uhdm::Any* const variable = object->getVariable()) {
  //   contextAny = getActual(variable);
  //   if (contextAny == nullptr) {
  //     visit(variable);
  //     contextAny = getActual(variable);
  //   }
  // }
  // if (contextAny == nullptr) return;
  //
  // const uhdm::Typespec* const typespec = getTypespec(contextAny);
  // if (typespec == nullptr) return;
  //
  // if (const uhdm::AnyCollection* const loopVars = object->getLoopVars()) {
  //   for (uhdm::Any* var : *loopVars) {
  //     if (uhdm::RefVar* const rv = any_cast<uhdm::RefVar>(var)) {
  //       if (uhdm::RefTypespec* const rt = rv->getTypespec()) {
  //         if ((rt->getActual() == nullptr) ||
  //             (rt->getActual()->getUhdmType() ==
  //              uhdm::UhdmType::UnsupportedTypespec)) {
  //           rt->setName(typespec->getName());
  //           rt->setActual(const_cast<uhdm::Typespec*>(typespec));
  //         }
  //       }
  //     }
  //   }
  // }
}

void ObjectBinder::visitHierPath(const uhdm::HierPath* object) {
  if (uhdm::AnyCollection* const pathElems = object->getPathElems()) {
    const uhdm::Any* prevAny = nullptr;
    for (const uhdm::Any* currAny : *pathElems) {
      const uhdm::BitSelect* const prevBitSelect =
          any_cast<uhdm::BitSelect>(prevAny);
      const uhdm::Any* currActual = uhdm::getActual(currAny);

      if ((currActual == nullptr) && (prevBitSelect != nullptr)) {
        if (any_cast<uhdm::BitSelect>(currAny) != nullptr) {
          currActual = prevBitSelect;
        }
      }

      if (currActual == nullptr) {
        bindAny(currAny);
      } else {
        uhdm::setActual(const_cast<uhdm::Any*>(currAny),
                        const_cast<uhdm::Any*>(currActual));
      }

      prevAny = currAny;
    }
  }
}

// void ObjectBinder::visitChandleVar(const uhdm::ChandleVar* const object) {
//   if (object->getActual() != nullptr) return;
//
//   if (const uhdm::Any* const actual = bindObject(object)) {
//     const_cast<uhdm::ChandleVar*>(object)->setActual(
//         const_cast<uhdm::Any*>(actual));
//   }
// }

void ObjectBinder::visitIndexedPartSelect(
    const uhdm::IndexedPartSelect* object) {
  if (object->getActual() != nullptr) return;

  if (const uhdm::Any* const actual = findObject(object)) {
    const_cast<uhdm::IndexedPartSelect*>(object)->setActual(
        const_cast<uhdm::Any*>(actual), true);
  }
}

void ObjectBinder::visitMethodFuncCall(const uhdm::MethodFuncCall* object) {
  if (object->getWith() == nullptr) return;

  const uhdm::HierPath* const hp = object->getParent<uhdm::HierPath>();
  if (hp == nullptr) return;

  const uhdm::Typespec* typespec = nullptr;
  if (const uhdm::AnyCollection* const elements = hp->getPathElems()) {
    uhdm::AnyCollection::const_iterator it =
        std::find(elements->cbegin(), elements->cend(), object);
    if ((it != elements->cbegin()) && (it != elements->cend())) {
      if (const uhdm::Any* const container = uhdm::getActual(*--it)) {
        typespec = uhdm::getTypespec(container);
      }

      if (const uhdm::ArrayTypespec* const at =
              any_cast<uhdm::ArrayTypespec>(typespec)) {
        typespec = uhdm::getElemTypespec(at);
      }
    }
  }

  if (typespec == nullptr) return;

  if (const uhdm::AnyCollection* const arguments = object->getArguments()) {
    for (const uhdm::Any* argument : *arguments) {
      if (const uhdm::RefObj* const ro = any_cast<uhdm::RefObj>(argument)) {
        if (ro->getActual() == nullptr) visitRefObj(ro);

        if (const uhdm::Variable* const v = ro->getActual<uhdm::Variable>()) {
          if (const uhdm::RefTypespec* const rt = v->getTypespec()) {
            if (rt->getActual() == nullptr) {
              const_cast<uhdm::RefTypespec*>(rt)->setActual(
                  const_cast<uhdm::Typespec*>(typespec), true);
            }
          }
        }
      }
    }
  }
}

void ObjectBinder::visitPartSelect(const uhdm::PartSelect* object) {
  if (object->getActual() != nullptr) return;

  if (const uhdm::Any* const actual = findObject(object)) {
    const_cast<uhdm::PartSelect*>(object)->setActual(
        const_cast<uhdm::Any*>(actual), true);
  }
}

void ObjectBinder::visitRefModule(const uhdm::RefModule* object) {
  if (object->getActual() != nullptr) return;

  if (const uhdm::Any* const actual = getModule(object->getDefName(), object)) {
    const_cast<uhdm::RefModule*>(object)->setActual(
        const_cast<uhdm::Instance*>(any_cast<uhdm::Instance>(actual)), true);
  } else if (const uhdm::Any* const actual =
                 getInterface(object->getDefName(), object)) {
    const_cast<uhdm::RefModule*>(object)->setActual(
        const_cast<uhdm::Instance*>(any_cast<uhdm::Instance>(actual)), true);
  }
}

void ObjectBinder::visitRefObj(const uhdm::RefObj* object) {
  if (object->getActual() != nullptr) return;

  if (object->getName() == "triggered") {
    if (const uhdm::HierPath* const hp = object->getParent<uhdm::HierPath>()) {
      if (const uhdm::AnyCollection* const pe = hp->getPathElems()) {
        uhdm::AnyCollection::const_iterator it =
            std::find(pe->cbegin(), pe->cend(), object);
        if ((it != pe->begin()) && (it != pe->cend())) {
          if (any_cast<uhdm::NamedEvent>(*--it) != nullptr) {
            // event.triggered is a property and can't be bound to anything.
            // SL doesn' model properties, yet!
            return;
          }
        }
      }
    }
  }

  if (const uhdm::Any* actual1 = findObject(object)) {
    // Reporting error for $root.
    if ((actual1->getUhdmType() == uhdm::UhdmType::Design) &&
        areSimilarNames(object, "$root"))
      return;

    const_cast<uhdm::RefObj*>(object)->setActual(
        const_cast<uhdm::Any*>(actual1), true);

    // Typespec for RefObj is the same as the typespec for its actual
    if (const uhdm::Typespec* const t = uhdm::getTypespec(actual1)) {
      if (t->getUhdmType() != uhdm::UhdmType::UnsupportedTypespec) {
        if (const uhdm::RefTypespec* const rt = object->getTypespec()) {
          if (rt->getActual<uhdm::UnsupportedTypespec>() != nullptr) {
            const_cast<uhdm::RefTypespec*>(rt)->setActual(
                const_cast<uhdm::Typespec*>(t), true);
          }
        }
      }
    }
  }
}

void ObjectBinder::visitRefTypespec(const uhdm::RefTypespec* object) {
  const uhdm::Typespec* const object_Actual_typespec = object->getActual();
  if ((object_Actual_typespec != nullptr) &&
      (object_Actual_typespec->getUhdmType() ==
       uhdm::UhdmType::UnsupportedTypespec)) {
    const_cast<uhdm::RefTypespec*>(object)->setActual(nullptr);
  }

  if (object->getActual() != nullptr) return;

  std::string_view name = object->getName();
  if (std::string_view::size_type pos = name.find('.');
      pos != std::string_view::npos) {
    std::string_view prefix = name.substr(0, pos);
    std::string_view suffix = name.substr(pos + 1);

    const uhdm::InterfaceTypespec* it = nullptr;
    if (const uhdm::Any* const a = find(prefix, RefType::Object, object)) {
      it = uhdm::getTypespec<uhdm::InterfaceTypespec>(a);
    } else if (const uhdm::Any* const t =
                   find(prefix, RefType::Typespec, object)) {
      it = any_cast<uhdm::InterfaceTypespec>(t);
    }

    if (it != nullptr) {
      if (const uhdm::Any* const actual =
              findInTypespec(suffix, RefType::Typespec, it)) {
        const_cast<uhdm::RefTypespec*>(object)->setActual(
            const_cast<uhdm::Typespec*>(any_cast<uhdm::Typespec>(actual)),
            true);
      }
    }
  } else if (const uhdm::Typespec* const actual = findType(object)) {
    const_cast<uhdm::RefTypespec*>(object)->setActual(
        const_cast<uhdm::Typespec*>(actual), true);
  }

  if ((object_Actual_typespec != nullptr) && (object->getActual() == nullptr)) {
    const_cast<uhdm::RefTypespec*>(object)->setActual(
        const_cast<uhdm::Typespec*>(object_Actual_typespec), true);
  }
}

void ObjectBinder::visitVarSelect(const uhdm::VarSelect* object) {
  if (object->getActual() != nullptr) return;

  if (const uhdm::Any* const actual = findObject(object)) {
    const_cast<uhdm::VarSelect*>(object)->setActual(
        const_cast<uhdm::Any*>(actual), true);
  }
}

const uhdm::Any* ObjectBinder::getHierPathElemPrefix(const uhdm::Any* object) {
  if (object == nullptr) return nullptr;

  const uhdm::Any* const parent = object->getParent();
  if (parent == nullptr) return nullptr;
  if (parent->getUhdmType() != uhdm::UhdmType::HierPath) return nullptr;

  const uhdm::HierPath* const hp = static_cast<const uhdm::HierPath*>(parent);
  if ((hp->getPathElems() == nullptr) || (hp->getPathElems()->size() < 2)) {
    return nullptr;
  }

  const uhdm::Any* previous = nullptr;
  for (size_t i = 1, n = hp->getPathElems()->size();
       (i < n) && (previous == nullptr); ++i) {
    if (hp->getPathElems()->at(i) == object) {
      previous = hp->getPathElems()->at(i - 1);
    }
  }
  if (previous == nullptr) return nullptr;

  const uhdm::RefObj* const ro = any_cast<uhdm::RefObj>(previous);
  if (ro == nullptr) return nullptr;

  if (areSimilarNames(ro, "this") || areSimilarNames(ro, "super")) {
    if (const uhdm::ClassDefn* const cd =
            uhdm::getParent<uhdm::ClassDefn>(ro)) {
      return cd;
    } else if (const uhdm::Interface* const i =
                   uhdm::getParent<uhdm::Interface>(ro)) {
      return i;
    }
    return nullptr;
  }

  if (const uhdm::ArrayTypespec* const at =
          uhdm::getTypespec<uhdm::ArrayTypespec>(ro)) {
    return uhdm::getElemTypespec(at);
  } else if (const uhdm::Net* const ln = ro->getActual<uhdm::Net>()) {
    // Ideally logic_net::Typespec should be valid but for
    // too many (or rather most) cases, the Typespec isn't set.
    // So, use the corresponding port in the parent module to
    // find the typespec.

    if (const uhdm::Typespec* const t = uhdm::getTypespec(ln)) {
      return t;
    } else if (const uhdm::Module* const m = ln->getParent<uhdm::Module>()) {
      if (m->getPorts() != nullptr) {
        for (const uhdm::Port* const p : *m->getPorts()) {
          if (const uhdm::RefObj* const ro = p->getLowConn<uhdm::RefObj>()) {
            if (ro->getActual() == ln) {
              return uhdm::getTypespec(p);
            }
          }
        }
      }
    }
  } else if (const uhdm::Any* const actual = uhdm::getActual(ro)) {
    if (const uhdm::Typespec* const typespec = uhdm::getTypespec(actual)) {
      return typespec;
    }
    return actual;
  }
  return nullptr;
}

const uhdm::Any* ObjectBinder::find(std::string_view name, RefType refType,
                                    const uhdm::Any* object) {
  m_searched.clear();
  m_searched.emplace(object);  // Prevent returning the same object

  if (std::string_view::size_type pos = name.find("::");
      pos != std::string::npos) {
    std::string_view prefixName = name.substr(0, pos);
    std::string_view suffixName = name.substr(pos + 2);

    if (const uhdm::Package* const p = getPackage(prefixName, object)) {
      if (const uhdm::Any* const actual =
              findInPackage(suffixName, refType, p)) {
        return actual;
      }
    } else if (const uhdm::ClassDefn* const cd =
                   getClassDefn(prefixName, object)) {
      if (const uhdm::Any* const actual =
              findInClassDefn(suffixName, refType, cd)) {
        return actual;
      }
    } else if (const uhdm::Typespec* const t = any_cast<uhdm::Typespec>(
                   find(prefixName, RefType::Typespec, object))) {
      if (const uhdm::Any* const actual =
              findInTypespec(suffixName, refType, t)) {
        return actual;
      }
    }

    m_unbounded.emplace(object);
    return nullptr;
  }

  const uhdm::Any* scope = object;
  if (const uhdm::Any* const prefix = getHierPathElemPrefix(object)) {
    // TODO(HS): This is incorrect. We can't just blindly ignore prefixes.
    // Naming for path elements of hier path needs to be fixed.
    std::string_view::size_type npos = name.find('.');
    if (npos != std::string_view::npos) name.remove_prefix(npos + 1);
    scope = prefix;
  }

  while (scope != nullptr) {
    switch (scope->getUhdmType()) {
      case uhdm::UhdmType::Function: {
        if (const uhdm::Any* const actual = findInFunction(
                name, refType, static_cast<const uhdm::Function*>(scope))) {
          return actual;
        }
      } break;

      case uhdm::UhdmType::Task: {
        if (const uhdm::Any* const actual = findInTask(
                name, refType, static_cast<const uhdm::Task*>(scope))) {
          return actual;
        }
      } break;

      case uhdm::UhdmType::ForStmt: {
        if (const uhdm::Any* const actual = findInForStmt(
                name, refType, static_cast<const uhdm::ForStmt*>(scope))) {
          return actual;
        }
      } break;

      case uhdm::UhdmType::ForeachStmt: {
        if (const uhdm::Any* const actual = findInForeachStmt(
                name, refType, static_cast<const uhdm::ForeachStmt*>(scope))) {
          return actual;
        }
      } break;

      case uhdm::UhdmType::Begin: {
        if (const uhdm::Any* const actual = findInScope_sub(
                name, refType, static_cast<const uhdm::Begin*>(scope))) {
          return actual;
        }
      } break;

      case uhdm::UhdmType::ForkStmt: {
        if (const uhdm::Any* const actual = findInScope_sub(
                name, refType, static_cast<const uhdm::ForkStmt*>(scope))) {
          return actual;
        }
      } break;

      case uhdm::UhdmType::ClassDefn: {
        if (const uhdm::Any* const actual = findInClassDefn(
                name, refType, static_cast<const uhdm::ClassDefn*>(scope))) {
          return actual;
        }
      } break;

      case uhdm::UhdmType::Module: {
        if (const uhdm::Any* const actual = findInModule(
                name, refType, static_cast<const uhdm::Module*>(scope))) {
          return actual;
        }
      } break;

      case uhdm::UhdmType::Interface: {
        if (const uhdm::Any* const actual = findInInterface(
                name, refType, static_cast<const uhdm::Interface*>(scope))) {
          return actual;
        }
      } break;

      case uhdm::UhdmType::Program: {
        if (const uhdm::Any* const actual = findInProgram(
                name, refType, static_cast<const uhdm::Program*>(scope))) {
          return actual;
        }
      } break;

      case uhdm::UhdmType::Package: {
        if (const uhdm::Any* const actual = findInPackage(
                name, refType, static_cast<const uhdm::Package*>(scope))) {
          return actual;
        }
      } break;

      case uhdm::UhdmType::UdpDefn: {
        if (const uhdm::Any* const actual = findInUdpDefn(
                name, refType, static_cast<const uhdm::UdpDefn*>(scope))) {
          return actual;
        }
      } break;

      case uhdm::UhdmType::Design: {
        if (const uhdm::Any* const actual = findInDesign(
                name, refType, static_cast<const uhdm::Design*>(scope))) {
          return actual;
        }
      } break;

      default: {
        if (const uhdm::Scope* const s = any_cast<uhdm::Scope>(scope)) {
          if (const uhdm::Any* const actual = findInScope(name, refType, s)) {
            return actual;
          }
        } else if (const uhdm::Typespec* const ts =
                       any_cast<uhdm::Typespec>(scope)) {
          if (const uhdm::Any* const actual =
                  findInTypespec(name, refType, ts)) {
            return actual;
          }
        }
      } break;
    }

    scope = scope->getParent();
  }

  for (ForwardComponentMap::const_reference entry : m_forwardComponentMap) {
    const DesignComponent* dc =
        valuedcomponenti_cast<DesignComponent>(entry.first);
    if (dc == nullptr) continue;

    const auto& fileContents = dc->getFileContents();
    if (!fileContents.empty()) {
      if (const FileContent* const fC = fileContents.front()) {
        for (const auto& td : fC->getTypeDefMap()) {
          const DataType* dt = td.second;
          while (dt != nullptr) {
            if (const uhdm::Typespec* const ts = dt->getTypespec()) {
              if (const uhdm::Any* const actual =
                      findInTypespec(name, refType, ts)) {
                return actual;
              }
            }
            dt = dt->getDefinition();
          }
        }
      }
    }
  }

  if (const uhdm::Package* const p = getPackage("builtin", object)) {
    if (const uhdm::Any* const actual = findInPackage(name, refType, p)) {
      return actual;
    }
  }

  m_unbounded.emplace(object);
  return nullptr;
}

const uhdm::Any* ObjectBinder::findObject(const uhdm::Any* object) {
  std::string_view name = object->getName();
  return name.empty() ? nullptr : find(name, RefType::Object, object);
}

const uhdm::Typespec* ObjectBinder::findType(const uhdm::Any* object) {
  std::string_view name = object->getName();
  return name.empty()
             ? nullptr
             : any_cast<uhdm::Typespec>(find(name, RefType::Typespec, object));
}

void ObjectBinder::reportErrors() {
  SymbolTable* const symbolTable = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  ErrorContainer* const errorContainer = m_session->getErrorContainer();

  for (const uhdm::Any* object : m_unbounded) {
    if (getDefaultNetType(object) == VObjectType::NO_TYPE) {
      const std::string text =
          StrCat("id:", object->getUhdmId(), ", name:", object->getName());
      Location loc(fileSystem->toPathId(object->getFile(), symbolTable),
                   object->getStartLine(), object->getStartColumn(),
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
  for (const uhdm::Any* object : unbounded) {
    const uhdm::RefObj* ro = any_cast<uhdm::RefObj>(object);
    if (ro == nullptr) continue;

    if (ro->getActual() == nullptr) {
      visitRefObj(ro);
    }

    if (ro->getActual() != nullptr) continue;

    const uhdm::Any* const pro = ro->getParent();
    bool tryRefTypespec = ro->getName().find("::") != std::string_view::npos;
    tryRefTypespec =
        tryRefTypespec ||
        ((pro != nullptr) &&
         (pro->getUhdmType() == uhdm::UhdmType::SysFuncCall) &&
         (areSimilarNames(pro, "$bits") || areSimilarNames(pro, "$typename")));

    if (tryRefTypespec) {
      uhdm::RefTypespec* const rt = m_serializer.make<uhdm::RefTypespec>();
      rt->setName(object->getName());
      rt->setParent(const_cast<uhdm::Any*>(pro));
      visitRefTypespec(rt);

      if (rt->getActual() == nullptr) {
        rt->setParent(nullptr);
        m_unbounded.erase(rt);
        m_serializer.erase(rt);
      } else {
        rt->setFile(ro->getFile());
        rt->setStartLine(ro->getStartLine());
        rt->setStartColumn(ro->getStartColumn());
        rt->setEndLine(ro->getEndLine());
        rt->setEndColumn(ro->getEndColumn());
        m_serializer.swap(object, rt);
        m_unbounded.erase(object);
        continue;
      }
    }

    VObjectType defaultNetType = getDefaultNetType(object);
    if (defaultNetType != VObjectType::NO_TYPE) {
      const uhdm::Scope* const parent = uhdm::getParent<uhdm::Scope>(object);

      uhdm::Net* const net = m_serializer.make<uhdm::Net>();
      net->setName(object->getName());
      net->setParent(const_cast<uhdm::Scope*>(parent));
      net->setNetType(UhdmWriter::getVpiNetType(defaultNetType));
      net->setFile(object->getFile());
      net->setStartLine(object->getStartLine());
      net->setStartColumn(object->getStartColumn());
      net->setEndLine(object->getEndLine());
      net->setEndColumn(object->getEndColumn());
      const_cast<uhdm::RefObj*>(ro)->setActual(net, true);
      result = true;
    }
  }
  return result;
}

void ObjectBinder::bind(const uhdm::Design* object, bool report) {
  uhdm::Serializer* const serializer = object->getSerializer();
  if (uhdm::Factory* const factory =
          serializer->getFactory<uhdm::RefTypespec>()) {
    for (uhdm::Any* source : factory->getObjects()) {
      uhdm::RefTypespec* const rt = any_cast<uhdm::RefTypespec>(source);
      if (rt->getActual<uhdm::UnsupportedTypespec>() != nullptr) {
        if (const uhdm::Typespec* const replacement = findType(source)) {
          rt->setActual(const_cast<uhdm::Typespec*>(replacement), true);
        }
      }
    }
  }

  visit(object);

  if (uhdm::Factory* const factory =
          serializer->getFactory<uhdm::ArrayTypespec>()) {
    for (uhdm::Any* source : factory->getObjects()) {
      uhdm::ArrayTypespec* const at = any_cast<uhdm::ArrayTypespec>(source);
      if (at->getArrayType() == vpiAssocArray) {
        if (uhdm::RangeCollection* const rc = at->getRanges()) {
          if (rc->size() == 1) {
            if (uhdm::Range* const r = rc->at(0)) {
              if (uhdm::Operation* const operation =
                      r->getRightExpr<uhdm::Operation>()) {
                if (uhdm::AnyCollection* const operands =
                        operation->getOperands()) {
                  if (operands->size() == 1) {
                    if (uhdm::RefObj* const operand =
                            any_cast<uhdm::RefObj>(operands->at(0))) {
                      bool switchToStaticArray = false;
                      uhdm::Typespec* const typespec =
                          uhdm::getTypespec(operand->getActual());
                      if (typespec != nullptr) {
                        switch (typespec->getUhdmType()) {
                          case uhdm::UhdmType::IntTypespec:
                          case uhdm::UhdmType::IntegerTypespec:
                          case uhdm::UhdmType::LongIntTypespec:
                          case uhdm::UhdmType::ShortIntTypespec: {
                            switchToStaticArray = true;
                          } break;
                          default:
                            break;
                        }
                      }

                      if (switchToStaticArray) {
                        at->setArrayType(vpiStaticArray);
                      } else {
                        uhdm::RefTypespec* const rt =
                            serializer->make<uhdm::RefTypespec>();
                        rt->setParent(at);
                        rt->setName(operand->getName());
                        rt->setFile(operand->getFile());
                        rt->setStartLine(operand->getStartLine());
                        rt->setStartColumn(operand->getStartColumn());
                        rt->setEndLine(operand->getEndLine());
                        rt->setEndColumn(operand->getEndColumn());
                        at->setIndexTypespec(rt);
                        at->setRanges(nullptr);
                        operand->setParent(nullptr);
                        visitRefTypespec(rt);
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  while (createDefaultNets()) {
    // Nothing to do here!
  }
  if (report) reportErrors();
}

void ObjectBinder::bind(const std::vector<const uhdm::Design*>& objects,
                        bool report) {
  for (const uhdm::Design* d : objects) {
    bind(d, report);
  }
}
}  // namespace SURELOG
