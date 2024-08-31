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
#include <uhdm/array_net.h>
#include <uhdm/assign_stmt.h>
#include <uhdm/begin.h>
#include <uhdm/bit_select.h>
#include <uhdm/class_defn.h>
#include <uhdm/class_typespec.h>
#include <uhdm/class_var.h>
#include <uhdm/constraint.h>
#include <uhdm/design.h>
#include <uhdm/enum_const.h>
#include <uhdm/enum_typespec.h>
#include <uhdm/enum_var.h>
#include <uhdm/extends.h>
#include <uhdm/for_stmt.h>
#include <uhdm/foreach_stmt.h>
#include <uhdm/fork_stmt.h>
#include <uhdm/function.h>
#include <uhdm/hier_path.h>
#include <uhdm/import_typespec.h>
#include <uhdm/indexed_part_select.h>
#include <uhdm/instance.h>
#include <uhdm/interface_array.h>
#include <uhdm/interface_inst.h>
#include <uhdm/interface_tf_decl.h>
#include <uhdm/interface_typespec.h>
#include <uhdm/io_decl.h>
#include <uhdm/logic_net.h>
#include <uhdm/modport.h>
#include <uhdm/module_inst.h>
#include <uhdm/package.h>
#include <uhdm/param_assign.h>
#include <uhdm/parameter.h>
#include <uhdm/part_select.h>
#include <uhdm/port.h>
#include <uhdm/program.h>
#include <uhdm/property_decl.h>
#include <uhdm/ref_module.h>
#include <uhdm/ref_obj.h>
#include <uhdm/ref_typespec.h>
#include <uhdm/struct_typespec.h>
#include <uhdm/struct_var.h>
#include <uhdm/sys_func_call.h>
#include <uhdm/task.h>
#include <uhdm/typespec.h>
#include <uhdm/typespec_member.h>
#include <uhdm/udp_defn.h>
#include <uhdm/union_typespec.h>

namespace SURELOG {
ObjectBinder::ObjectBinder(const ComponentMap& componentMap,
                           UHDM::Serializer& serializer,
                           SymbolTable* const symbolTable,
                           ErrorContainer* const errorContainer,
                           bool muteStdout)
    : m_componentMap(componentMap),
      m_serializer(serializer),
      m_symbolTable(symbolTable),
      m_errorContainer(errorContainer),
      m_muteStdout(muteStdout) {
  for (ComponentMap::const_reference entry : m_componentMap) {
    m_baseclassMap.emplace(entry.second, entry.first);
  }
}

inline std::string_view ObjectBinder::suffixName(std::string_view varg) const {
  size_t pos1 = varg.find("::");
  if (pos1 != std::string::npos) {
    varg = varg.substr(pos1 + 2);
  }

  size_t pos2 = varg.find("work@");
  if (pos2 != std::string::npos) {
    varg = varg.substr(pos2 + 5);
  }
  return varg;
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
      if (p->VpiName() == name) {
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

const UHDM::class_defn* ObjectBinder::getClass_defn(
    std::string_view name) const {
  if (!m_packageStack.empty()) {
    const UHDM::package* const p = m_packageStack.back();
    if (p->Class_defns() != nullptr) {
      for (const UHDM::class_defn* c : *p->Class_defns()) {
        if (c->VpiName() == name) {
          return c;
        }
      }
    }
  }
  if (!m_designStack.empty()) {
    const UHDM::design* const d = m_designStack.back();
    if (d->AllClasses() != nullptr) {
      for (const UHDM::class_defn* c : *d->AllClasses()) {
        if (c->VpiName() == name) {
          return c;
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
        if (const UHDM::any* const actual = findInPackage(object, p)) {
          return actual;
        }
      }
    } break;

    case UHDM::uhdmclass_typespec: {
      if (const UHDM::class_defn* cd =
              static_cast<const UHDM::class_typespec*>(scope)->Class_defn()) {
        if (const UHDM::any* const actual = findInClass_defn(object, cd)) {
          return actual;
        }
      }
    } break;

    case UHDM::uhdminterface_typespec: {
      if (const UHDM::interface_inst* ins =
              static_cast<const UHDM::interface_typespec*>(scope)
                  ->Interface_inst()) {
        if (const UHDM::any* const actual = findInInterface_inst(object, ins)) {
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
    if (c->UhdmType() == UHDM::uhdmbit_select) continue;
    if (c->UhdmType() == UHDM::uhdmindexed_part_select) continue;
    if (c->UhdmType() == UHDM::uhdmpart_select) continue;
    if (c->UhdmType() == UHDM::uhdmref_module) continue;
    if (c->UhdmType() == UHDM::uhdmref_obj) continue;
    if (c->UhdmType() == UHDM::uhdmvar_select) continue;

    if (any_cast<UHDM::ref_typespec>(object) != nullptr) {
      if (any_cast<UHDM::typespec>(c) != nullptr) {
        if (c->VpiName() == name) return c;
        if (c->VpiName() == shortName) return c;
      }
    } else {
      if (c->VpiName() == name) return c;
      if (c->VpiName() == shortName) return c;
    }

    if (any_cast<UHDM::typespec>(c) != nullptr) {
      if (const UHDM::any* const actual1 = findInTypespec(
              name, object, static_cast<const UHDM::typespec*>(c))) {
        return actual1;
      }
    }

    if (c->UhdmType() == UHDM::uhdmenum_var) {
      if (const UHDM::any* const actual1 = findInRefTypespec(
              name, object,
              static_cast<const UHDM::enum_var*>(c)->Typespec())) {
        return actual1;
      } else if (const UHDM::any* const actual2 = findInRefTypespec(
                     shortName, object,
                     static_cast<const UHDM::enum_var*>(c)->Typespec())) {
        return actual2;
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
        if (const UHDM::any* const actual1 =
                findInRefTypespec(name, object, rt)) {
          return actual1;
        } else if (const UHDM::any* const actual2 =
                       findInRefTypespec(shortName, object, rt)) {
          return actual2;
        }
      }
    }
  }

  return nullptr;
}

const UHDM::any* ObjectBinder::findInScope(const UHDM::any* const object,
                                           const UHDM::scope* const scope) {
  if (scope == nullptr) return nullptr;

  std::string_view name = object->VpiName();
  if (const UHDM::any* const actual1 =
          findInVectorOfAny(name, object, scope->Variables(), scope)) {
    return actual1;
  } else if (const UHDM::any* const actual2 = findInVectorOfAny(
                 name, object, scope->Param_assigns(), scope)) {
    return actual2;
  } else if (const UHDM::any* const actual3 =
                 findInVectorOfAny(name, object, scope->Parameters(), scope)) {
    return actual3;
  } else if (const UHDM::any* const actual3 = findInVectorOfAny(
                 name, object, scope->Property_decls(), scope)) {
    return actual3;
  } else if (const UHDM::any* const actual4 =
                 findInVectorOfAny(name, object, scope->Typespecs(), scope)) {
    return actual4;
  } else if (const UHDM::package* const p = any_cast<UHDM::package>(scope)) {
    std::string fullName = StrCat(p->VpiName(), "::", name);
    if (const UHDM::any* const actual =
            findInVectorOfAny(fullName, object, scope->Typespecs(), scope)) {
      return actual;
    }
  }

  return nullptr;
}

const UHDM::any* ObjectBinder::findInInstance(
    const UHDM::any* const object, const UHDM::instance* const scope) {
  if (scope == nullptr) return nullptr;

  std::string_view name = object->VpiName();
  if (scope->VpiName() == name) return scope;

  if (const UHDM::any* const actual1 =
          findInVectorOfAny(name, object, scope->Nets(), scope)) {
    return actual1;
  } else if (const UHDM::any* const actual2 =
                 findInVectorOfAny(name, object, scope->Array_nets(), scope)) {
    return actual2;
  } else if (const UHDM::any* const actual3 =
                 findInVectorOfAny(name, object, scope->Task_funcs(), scope)) {
    return actual3;
  } else if (const UHDM::any* const actual4 =
                 findInVectorOfAny(name, object, scope->Programs(), scope)) {
    return actual4;
  }

  return findInScope(object, scope);
}

const UHDM::any* ObjectBinder::findInInterface_inst(
    const UHDM::any* const object, const UHDM::interface_inst* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  std::string_view name = object->VpiName();
  if (scope->VpiName() == name) {
    return scope;
  } else if (const UHDM::any* const actual1 =
                 findInVectorOfAny(name, object, scope->Modports(), scope)) {
    return actual1;
  } else if (const UHDM::any* const actual4 = findInVectorOfAny(
                 name, object, scope->Interface_tf_decls(), scope)) {
    return actual4;
  } else if (const UHDM::any* const actual4 =
                 findInVectorOfAny(name, object, scope->Ports(), scope)) {
    return actual4;
  }
  return findInInstance(object, scope);
}

const UHDM::any* ObjectBinder::findInPackage(const UHDM::any* const object,
                                             const UHDM::package* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  std::string_view name = object->VpiName();
  if (scope->VpiName() == name) return scope;

  if (const UHDM::any* const actual =
          findInVectorOfAny(name, object, scope->Parameters(), scope)) {
    return actual;
  }

  return findInInstance(object, scope);
}

const UHDM::any* ObjectBinder::findInUdp_defn(
    const UHDM::any* const object, const UHDM::udp_defn* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  std::string_view name = object->VpiName();
  if (scope->VpiName() == name) return scope;
  return findInVectorOfAny(name, object, scope->Io_decls(), scope);
}

const UHDM::any* ObjectBinder::findInProgram(const UHDM::any* const object,
                                             const UHDM::program* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  std::string_view name = object->VpiName();
  if (scope->VpiName() == name) return scope;

  if (const UHDM::any* const actual =
          findInVectorOfAny(name, object, scope->Parameters(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Ports(), scope)) {
    return actual;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->Interfaces(), scope)) {
    return actual;
  }

  return findInInstance(object, scope);
}

const UHDM::any* ObjectBinder::findInFunction(
    const UHDM::any* const object, const UHDM::function* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  std::string_view name = object->VpiName();
  if (scope->VpiName() == name) {
    return scope->Return();
  } else if (const UHDM::any* const actual1 =
                 findInVectorOfAny(name, object, scope->Io_decls(), scope)) {
    return actual1;
  } else if (const UHDM::any* const actual2 =
                 findInVectorOfAny(name, object, scope->Variables(), scope)) {
    return actual2;
  } else if (const UHDM::any* const actual3 =
                 findInVectorOfAny(name, object, scope->Parameters(), scope)) {
    return actual3;
  } else if (const UHDM::package* const inst =
                 scope->Instance<UHDM::package>()) {
    if (const UHDM::any* const actual = findInPackage(object, inst)) {
      return actual;
    }
  }

  return findInScope(object, scope);
}

const UHDM::any* ObjectBinder::findInTask(const UHDM::any* const object,
                                          const UHDM::task* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  std::string_view name = object->VpiName();
  if (scope->VpiName() == name) return scope;

  if (const UHDM::any* const actual1 =
          findInVectorOfAny(name, object, scope->Io_decls(), scope)) {
    return actual1;
  } else if (const UHDM::any* const actual2 =
                 findInVectorOfAny(name, object, scope->Variables(), scope)) {
    return actual2;
  } else if (const UHDM::package* const p = scope->Instance<UHDM::package>()) {
    if (const UHDM::any* const actual = findInPackage(object, p)) {
      return actual;
    }
  }

  return findInScope(object, scope);
}

const UHDM::any* ObjectBinder::findInFor_stmt(
    const UHDM::any* const object, const UHDM::for_stmt* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  const std::string_view name = object->VpiName();
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
        if (lhs->UhdmType() == UHDM::uhdmbit_select) continue;
        if (lhs->UhdmType() == UHDM::uhdmindexed_part_select) continue;
        if (lhs->UhdmType() == UHDM::uhdmpart_select) continue;
        if (lhs->UhdmType() == UHDM::uhdmref_module) continue;
        if (lhs->UhdmType() == UHDM::uhdmref_obj) continue;
        if (lhs->UhdmType() == UHDM::uhdmvar_select) continue;
        if (lhs->VpiName() == name) return lhs;
        if (lhs->VpiName() == shortName) return lhs;
      }
    }
  }

  return findInScope(object, scope);
}

const UHDM::any* ObjectBinder::findInForeach_stmt(
    const UHDM::any* const object, const UHDM::foreach_stmt* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  std::string_view name = object->VpiName();
  if (const UHDM::any* const var =
          findInVectorOfAny(name, object, scope->VpiLoopVars(), scope)) {
    return var;
  }

  return findInScope(object, scope);
}

template <typename T>
const UHDM::any* ObjectBinder::findInScope_sub(
    const UHDM::any* const object, const T* const scope,
    typename std::enable_if<
        std::is_same<UHDM::begin, typename std::decay<T>::type>::value ||
        std::is_same<UHDM::fork_stmt,
                     typename std::decay<T>::type>::value>::type* /* = 0 */) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  const std::string_view name = object->VpiName();
  if (const UHDM::any* const actual1 =
          findInVectorOfAny(name, object, scope->Variables(), scope)) {
    return actual1;
  } else if (const UHDM::any* const actual2 =
                 findInVectorOfAny(name, object, scope->Parameters(), scope)) {
    return actual2;
  }

  std::string_view shortName = name;
  if (shortName.find("::") != std::string::npos) {
    std::vector<std::string_view> tokens;
    StringUtils::tokenizeMulti(shortName, "::", tokens);
    if (tokens.size() > 1) shortName = tokens.back();
  }

  if (const UHDM::VectorOfany* const stmts = scope->Stmts()) {
    for (auto init : *stmts) {
      if (init->UhdmType() == UHDM::uhdmassign_stmt) {
        const UHDM::expr* const lhs =
            static_cast<const UHDM::assign_stmt*>(init)->Lhs();
        if (lhs->UhdmType() == UHDM::uhdmbit_select) continue;
        if (lhs->UhdmType() == UHDM::uhdmindexed_part_select) continue;
        if (lhs->UhdmType() == UHDM::uhdmpart_select) continue;
        if (lhs->UhdmType() == UHDM::uhdmref_module) continue;
        if (lhs->UhdmType() == UHDM::uhdmref_obj) continue;
        if (lhs->UhdmType() == UHDM::uhdmvar_select) continue;
        if (lhs->VpiName() == name) return lhs;
        if (lhs->VpiName() == shortName) return lhs;
      }
    }
  }

  return findInScope(object, scope);
}

const UHDM::any* ObjectBinder::findInClass_defn(
    const UHDM::any* const object, const UHDM::class_defn* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  const std::string_view name = object->VpiName();
  std::string_view shortName = name;
  if (shortName.find("::") != std::string::npos) {
    std::vector<std::string_view> tokens;
    StringUtils::tokenizeMulti(shortName, "::", tokens);
    if (tokens.size() > 1) shortName = tokens.back();
  }

  if (name == "this") return scope;

  if (name == "super") {
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

  if (scope->VpiName() == name) return scope;
  if (scope->VpiName() == shortName) return scope;

  if (const UHDM::any* const actual1 =
          findInVectorOfAny(name, object, scope->Variables(), scope)) {
    return actual1;
  } else if (const UHDM::any* const actual2 =
                 findInVectorOfAny(name, object, scope->Task_funcs(), scope)) {
    return actual2;
  } else if (const UHDM::any* const actual3 =
                 findInScope(object, static_cast<const UHDM::scope*>(scope))) {
    return actual3;
  } else if (const UHDM::any* const actual4 =
                 findInVectorOfAny(name, object, scope->Constraints(), scope)) {
    return actual4;
  } else if (const UHDM::extends* ext = scope->Extends()) {
    if (const UHDM::ref_typespec* rt = ext->Class_typespec()) {
      if (const UHDM::class_typespec* cts =
              rt->Actual_typespec<UHDM::class_typespec>()) {
        return findInClass_defn(object, cts->Class_defn());
      }
    }
  }

  return findInScope(object, scope);
}

const UHDM::any* ObjectBinder::findInModule_inst(
    const UHDM::any* const object, const UHDM::module_inst* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  std::string_view name = suffixName(object->VpiName());
  if (name == suffixName(scope->VpiName())) {
    return scope;
  } else if (const UHDM::any* const actual1 =
                 findInVectorOfAny(name, object, scope->Interfaces(), scope)) {
    return actual1;
  } else if (const UHDM::any* const actual2 = findInVectorOfAny(
                 name, object, scope->Interface_arrays(), scope)) {
    return actual2;
  }

  return findInInstance(object, scope);
}

const UHDM::any* ObjectBinder::findInDesign(const UHDM::any* const object,
                                            const UHDM::design* const scope) {
  if (scope == nullptr) return nullptr;
  if (!m_searched.emplace(scope).second) return nullptr;

  const std::string_view name = object->VpiName();

  if (name == "$root") {
    return scope;
  } else if (scope->VpiName() == name) {
    return scope;
  } else if (const UHDM::any* const actual1 =
                 findInVectorOfAny(name, object, scope->Parameters(), scope)) {
    return actual1;
  } else if (const UHDM::any* const actual2 = findInVectorOfAny(
                 name, object, scope->Param_assigns(), scope)) {
    return actual2;
  } else if (const UHDM::any* const actual =
                 findInVectorOfAny(name, object, scope->AllPackages(), scope)) {
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
              if ((ro1->VpiName() == "this") || (ro1->VpiName() == "super")) {
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
                  return ts;
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
    }
  }

  while (parent != nullptr) {
    switch (parent->UhdmType()) {
      case UHDM::uhdmfunction: {
        if (const UHDM::any* const actual = findInFunction(
                object, static_cast<const UHDM::function*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmtask: {
        if (const UHDM::any* const actual =
                findInTask(object, static_cast<const UHDM::task*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmfor_stmt: {
        if (const UHDM::any* const actual = findInFor_stmt(
                object, static_cast<const UHDM::for_stmt*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmforeach_stmt: {
        if (const UHDM::any* const actual = findInForeach_stmt(
                object, static_cast<const UHDM::foreach_stmt*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmbegin: {
        if (const UHDM::any* const actual = findInScope_sub(
                object, static_cast<const UHDM::begin*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmfork_stmt: {
        if (const UHDM::any* const actual = findInScope_sub(
                object, static_cast<const UHDM::fork_stmt*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmclass_defn: {
        if (const UHDM::any* const actual = findInClass_defn(
                object, static_cast<const UHDM::class_defn*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmmodule_inst: {
        if (const UHDM::any* const actual = findInModule_inst(
                object, static_cast<const UHDM::module_inst*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdminterface_inst: {
        if (const UHDM::any* const actual = findInInterface_inst(
                object, static_cast<const UHDM::interface_inst*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmprogram: {
        if (const UHDM::any* const actual = findInProgram(
                object, static_cast<const UHDM::program*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmpackage: {
        if (const UHDM::any* const actual = findInPackage(
                object, static_cast<const UHDM::package*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmudp_defn: {
        if (const UHDM::any* const actual = findInUdp_defn(
                object, static_cast<const UHDM::udp_defn*>(parent))) {
          return actual;
        }
      } break;

      case UHDM::uhdmdesign: {
        if (const UHDM::any* const actual = findInDesign(
                object, static_cast<const UHDM::design*>(parent))) {
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

void ObjectBinder::enterRef_module(const UHDM::ref_module* const object) {
  if (object->Actual_group() != nullptr) return;

  if (const UHDM::any* const actual = getModuleInst(object->VpiDefName())) {
    const_cast<UHDM::ref_module*>(object)->Actual_group(
        const_cast<UHDM::any*>(actual));
  }
}

void ObjectBinder::enterRef_obj(const UHDM::ref_obj* const object) {
  if (object->Actual_group() != nullptr) return;

  if (const UHDM::any* actual = bindObject(object)) {
    // Reporting error for $root.
    if ((object->VpiName() == "$root") &&
        (actual->UhdmType() == UHDM::uhdmdesign))
      return;

    const_cast<UHDM::ref_obj*>(object)->Actual_group(
        const_cast<UHDM::any*>(actual));
  }
}

void ObjectBinder::enterRef_typespec(const UHDM::ref_typespec* const object) {
  if (const UHDM::typespec* const t = object->Actual_typespec()) {
    if (t->UhdmType() != UHDM::uhdmunsupported_typespec) {
      return;
    }
  }

  if (const UHDM::any* actual = bindObject(object)) {
    const_cast<UHDM::ref_typespec*>(object)->Actual_typespec(
        const_cast<UHDM::typespec*>(any_cast<UHDM::typespec>(actual)));
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
    if (rt->VpiName() != dc->getName()) continue;

    const ClassDefinition* bdef =
        valuedcomponenti_cast<ClassDefinition>(entry.first);

    if (bdef != nullptr) {
      // const DataType* the_def = bdef->getDataType(rt->VpiName());
      // Property* prop = new Property(
      //     the_def, bdef->getFileContent(), bdef->getNodeId(),
      //     InvalidNodeId, "super",
      //                  false, false, false, false, false);
      // bdef->insertProperty(prop);

      if (UHDM::class_defn* base = bdef->getUhdmScope<UHDM::class_defn>()) {
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
  FileSystem* const fileSystem = FileSystem::getInstance();
  for (auto& [object, scope, component] : m_unbounded) {
    bool reportMissingActual = false;
    if (const UHDM::ref_obj* const ro = any_cast<UHDM::ref_obj>(object)) {
      if (ro->Actual_group() == nullptr) {
        if (const UHDM::any* const parent = object->VpiParent()) {
          if (!(((object->VpiName() == "size") ||
                 (object->VpiName() == "delete")) &&
                (parent->UhdmType() == UHDM::uhdmhier_path)))
            reportMissingActual = true;
          if (object->VpiName() != "default") reportMissingActual = true;
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
      if (rm->Actual_group() == nullptr) {
        reportMissingActual = true;
      }
    }

    if (reportMissingActual) {
      const std::string text =
          StrCat("id: ", object->UhdmId(), ", name: ", object->VpiName());
      Location loc(fileSystem->toPathId(object->VpiFile(), m_symbolTable),
                   object->VpiLineNo(), object->VpiColumnNo(),
                   m_symbolTable->registerSymbol(text));
      Error err(ErrorDefinition::UHDM_FAILED_TO_BIND, loc);
      m_errorContainer->addError(err);
      m_errorContainer->printMessages(m_muteStdout);
    }

    if (getDefaultNetType(component) == VObjectType::slNoType) {
      const std::string text =
          StrCat("id:", object->UhdmId(), ", name: ", object->VpiName());
      Location loc(fileSystem->toPathId(object->VpiFile(), m_symbolTable),
                   object->VpiLineNo(), object->VpiColumnNo(),
                   m_symbolTable->registerSymbol(text));
      Error err(ErrorDefinition::ELAB_ILLEGAL_IMPLICIT_NET, loc);
      m_errorContainer->addError(err);
      m_errorContainer->printMessages(m_muteStdout);
    }
  }
}

bool ObjectBinder::createDefaultNets() {
  bool result = false;
  Unbounded unbounded(m_unbounded);
  m_unbounded.clear();
  for (Unbounded::const_reference entry : unbounded) {
    const UHDM::any* const object = std::get<0>(entry);
    UHDM::instance* const scope =
        const_cast<UHDM::instance*>(std::get<1>(entry));
    const ValuedComponentI* const component = std::get<2>(entry);

    const UHDM::ref_obj* ro = any_cast<UHDM::ref_obj>(object);
    if (ro == nullptr) continue;

    if (ro->Actual_group() == nullptr) {
      enterRef_obj(ro);
    }

    if (ro->Actual_group() != nullptr) continue;

    const UHDM::any* const pro = ro->VpiParent();
    if ((pro != nullptr) && (pro->UhdmType() == UHDM::uhdmsys_func_call) &&
        (pro->VpiName() == "$bits")) {
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
    if ((defaultNetType != VObjectType::slNoType) && (scope != nullptr)) {
      UHDM::logic_net* net = m_serializer.MakeLogic_net();
      net->VpiName(object->VpiName());
      net->VpiParent(scope);
      net->VpiNetType(UhdmWriter::getVpiNetType(defaultNetType));
      net->VpiLineNo(object->VpiLineNo());
      net->VpiColumnNo(object->VpiColumnNo());
      net->VpiEndLineNo(object->VpiEndLineNo());
      net->VpiEndColumnNo(object->VpiEndColumnNo());
      net->VpiFile(object->VpiFile());
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
