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
 * File:   UhdmWriter.cpp
 * Author: alain
 *
 * Created on January 17, 2020, 9:13 PM
 */

#include <Surelog/CommandLine/CommandLineParser.h>
#include <Surelog/Common/FileSystem.h>
#include <Surelog/Design/DesignElement.h>
#include <Surelog/Design/FileContent.h>
#include <Surelog/Design/ModPort.h>
#include <Surelog/Design/ModuleDefinition.h>
#include <Surelog/Design/ModuleInstance.h>
#include <Surelog/Design/Netlist.h>
#include <Surelog/Design/Parameter.h>
#include <Surelog/Design/Signal.h>
#include <Surelog/DesignCompile/CompileDesign.h>
#include <Surelog/DesignCompile/UhdmChecker.h>
#include <Surelog/DesignCompile/UhdmWriter.h>
#include <Surelog/Package/Package.h>
#include <Surelog/SourceCompile/Compiler.h>
#include <Surelog/SourceCompile/SymbolTable.h>
#include <Surelog/Testbench/ClassDefinition.h>
#include <Surelog/Testbench/Program.h>
#include <Surelog/Testbench/TypeDef.h>
#include <Surelog/Testbench/Variable.h>
#include <Surelog/Utils/StringUtils.h>

#include <algorithm>
#include <cstring>
#include <queue>
#include <set>

// UHDM
#include <uhdm/ElaboratorListener.h>
#include <uhdm/ExprEval.h>
#include <uhdm/Serializer.h>
#include <uhdm/SynthSubset.h>
#include <uhdm/UhdmAdjuster.h>
#include <uhdm/UhdmLint.h>
#include <uhdm/UhdmListener.h>
#include <uhdm/VpiListener.h>
#include <uhdm/clone_tree.h>
#include <uhdm/uhdm.h>
#include <uhdm/vpi_visitor.h>

namespace SURELOG {
namespace fs = std::filesystem;
using namespace UHDM;  // NOLINT (we're using a whole bunch of these)

class ObjectBinder final : public UHDM::UhdmListener {
 private:
  typedef std::map<const UHDM::BaseClass*, const ValuedComponentI*>
      BaseClassMap;
  typedef std::map<const ValuedComponentI*, UHDM::BaseClass*> ComponentMap;
  typedef std::vector<const UHDM::design*> DesignStack;
  typedef std::vector<const UHDM::package*> PackageStack;
  typedef std::vector<const UHDM::any*> PrefixStack;
  typedef std::vector<std::tuple<const UHDM::any*, const UHDM::instance*,
                                 const ValuedComponentI*>>
      Unbounded;
  typedef std::set<const UHDM::any*> Searched;

 public:
  ObjectBinder(const ComponentMap& componentMap, Serializer& serializer,
               SymbolTable* const symbolTable,
               ErrorContainer* const errorContainer, bool muteStdout)
      : m_componentMap(componentMap),
        m_serializer(serializer),
        m_symbolTable(symbolTable),
        m_errorContainer(errorContainer),
        m_muteStdout(muteStdout) {
    for (ComponentMap::const_reference entry : m_componentMap) {
      m_baseclassMap.emplace(entry.second, entry.first);
    }
  }

  inline std::string_view suffixName(std::string_view varg) const {
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

  static bool isInElaboratedTree(const UHDM::any* const object) {
    const UHDM::any* p = object;
    while (p != nullptr) {
      if (const UHDM::instance* const inst =
              any_cast<const UHDM::instance*>(p)) {
        if (inst->VpiTop()) return true;
      }
      p = p->VpiParent();
    }
    return false;
  }

  VObjectType getDefaultNetType(const ValuedComponentI* component) const {
    if (component == nullptr) return VObjectType::slNoType;

    if (const DesignComponent* dc1 =
            valuedcomponenti_cast<const DesignComponent*>(component)) {
      if (const DesignElement* de = dc1->getDesignElement()) {
        return de->m_defaultNetType;
      }
    }

    if (const ModuleInstance* mi =
            valuedcomponenti_cast<const ModuleInstance*>(component)) {
      if (const DesignComponent* dc2 =
              valuedcomponenti_cast<const DesignComponent*>(
                  mi->getDefinition())) {
        if (const DesignElement* de = dc2->getDesignElement()) {
          return de->m_defaultNetType;
        }
      }
    }

    return VObjectType::slNoType;
  }

  const UHDM::package* getPackage(std::string_view name) const {
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

  const UHDM::class_defn* getClass_defn(std::string_view name) const {
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

  void enterDesign(const UHDM::design* const object) final {
    m_designStack.emplace_back(object);
  }

  void leaveDesign(const UHDM::design* const object) final {
    m_designStack.pop_back();
  }

  void enterPackage(const UHDM::package* const object) final {
    m_packageStack.emplace_back(object);
  }

  void leavePackage(const UHDM::package* const object) final {
    m_packageStack.pop_back();
  }

  void enterHier_path(const UHDM::hier_path* const object) final {
    m_prefixStack.emplace_back(object);
  }

  void leaveHier_path(const UHDM::hier_path* const object) final {
    if (!m_prefixStack.empty() && (m_prefixStack.back() == object)) {
      m_prefixStack.pop_back();
    }
  }

  const UHDM::any* findInTypespec(std::string_view name,
                                  const UHDM::any* const object,
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
            any_cast<const UHDM::import_typespec*>(scope);
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
          if (const UHDM::any* const actual =
                  findInInterface_inst(object, ins)) {
            return actual;
          }
        }
      } break;

      default:
        break;
    }

    return nullptr;
  }

  const UHDM::any* findInRefTypespec(std::string_view name,
                                     const UHDM::any* const object,
                                     const UHDM::ref_typespec* const scope) {
    if (scope == nullptr) return nullptr;

    if (const UHDM::typespec* const ts = scope->Actual_typespec()) {
      return findInTypespec(name, object, ts);
    }
    return nullptr;
  }

  template <typename T>
  const UHDM::any* findInVectorOfAny(std::string_view name,
                                     const UHDM::any* const object,
                                     const std::vector<T*>* const collection,
                                     const UHDM::any* const scope) {
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
      if (const UHDM::ref_typespec* rt =
              any_cast<const UHDM::ref_typespec*>(c)) {
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

  const UHDM::any* findInScope(const UHDM::any* const object,
                               const UHDM::scope* const scope) {
    if (scope == nullptr) return nullptr;

    std::string_view name = object->VpiName();
    if (const UHDM::any* const actual1 =
            findInVectorOfAny(name, object, scope->Variables(), scope)) {
      return actual1;
    } else if (const UHDM::any* const actual2 = findInVectorOfAny(
                   name, object, scope->Param_assigns(), scope)) {
      return actual2;
    } else if (const UHDM::any* const actual3 = findInVectorOfAny(
                   name, object, scope->Parameters(), scope)) {
      return actual3;
    } else if (const UHDM::any* const actual3 = findInVectorOfAny(
                   name, object, scope->Property_decls(), scope)) {
      return actual3;
    } else if (const UHDM::any* const actual4 =
                   findInVectorOfAny(name, object, scope->Typespecs(), scope)) {
      return actual4;
    } else if (const UHDM::package* const p =
                   any_cast<const UHDM::package*>(scope)) {
      std::string fullName = StrCat(p->VpiName(), "::", name);
      if (const UHDM::any* const actual =
              findInVectorOfAny(fullName, object, scope->Typespecs(), scope)) {
        return actual;
      }
    }

    return nullptr;
  }

  const UHDM::any* findInInstance(const UHDM::any* const object,
                                  const UHDM::instance* const scope) {
    if (scope == nullptr) return nullptr;

    std::string_view name = object->VpiName();
    if (scope->VpiName() == name) return scope;

    if (const UHDM::any* const actual1 =
            findInVectorOfAny(name, object, scope->Nets(), scope)) {
      return actual1;
    } else if (const UHDM::any* const actual2 = findInVectorOfAny(
                   name, object, scope->Array_nets(), scope)) {
      return actual2;
    } else if (const UHDM::any* const actual3 = findInVectorOfAny(
                   name, object, scope->Task_funcs(), scope)) {
      return actual3;
    } else if (const UHDM::any* const actual4 =
                   findInVectorOfAny(name, object, scope->Programs(), scope)) {
      return actual4;
    }

    return findInScope(object, scope);
  }

  const UHDM::any* findInInterface_inst(
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

  const UHDM::any* findInPackage(const UHDM::any* const object,
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

  const UHDM::any* findInProgram(const UHDM::any* const object,
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
    } else if (const UHDM::any* const actual = findInVectorOfAny(
                   name, object, scope->Interfaces(), scope)) {
      return actual;
    }

    return findInInstance(object, scope);
  }

  const UHDM::any* findInFunction(const UHDM::any* const object,
                                  const UHDM::function* const scope) {
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
    } else if (const UHDM::any* const actual3 = findInVectorOfAny(
                   name, object, scope->Parameters(), scope)) {
      return actual3;
    } else if (const UHDM::package* const inst =
                   scope->Instance<UHDM::package>()) {
      if (const UHDM::any* const actual = findInPackage(object, inst)) {
        return actual;
      }
    }

    return findInScope(object, scope);
  }

  const UHDM::any* findInTask(const UHDM::any* const object,
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
    } else if (const UHDM::package* const p =
                   scope->Instance<UHDM::package>()) {
      if (const UHDM::any* const actual = findInPackage(object, p)) {
        return actual;
      }
    }

    return findInScope(object, scope);
  }

  const UHDM::any* findInFor_stmt(const UHDM::any* const object,
                                  const UHDM::for_stmt* const scope) {
    if (scope == nullptr) return nullptr;
    if (!m_searched.emplace(scope).second) return nullptr;

    const std::string_view name = object->VpiName();
    std::string_view shortName = name;
    if (shortName.find("::") != std::string::npos) {
      std::vector<std::string_view> tokens;
      StringUtils::tokenizeMulti(shortName, "::", tokens);
      if (tokens.size() > 1) shortName = tokens.back();
    }

    if (const VectorOfany* const inits = scope->VpiForInitStmts()) {
      for (auto init : *inits) {
        if (init->UhdmType() == uhdmassign_stmt) {
          const expr* const lhs = static_cast<const assign_stmt*>(init)->Lhs();
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

  const UHDM::any* findInForeach_stmt(const UHDM::any* const object,
                                      const UHDM::foreach_stmt* const scope) {
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
  const UHDM::any* findInScope_sub(
      const UHDM::any* const object, const T* const scope,
      typename std::enable_if<
          std::is_same<UHDM::begin, typename std::decay<T>::type>::value ||
          std::is_same<UHDM::fork_stmt,
                       typename std::decay<T>::type>::value>::type* = 0) {
    if (scope == nullptr) return nullptr;
    if (!m_searched.emplace(scope).second) return nullptr;

    const std::string_view name = object->VpiName();
    if (const UHDM::any* const actual1 =
            findInVectorOfAny(name, object, scope->Variables(), scope)) {
      return actual1;
    } else if (const UHDM::any* const actual2 = findInVectorOfAny(
                   name, object, scope->Parameters(), scope)) {
      return actual2;
    }

    std::string_view shortName = name;
    if (shortName.find("::") != std::string::npos) {
      std::vector<std::string_view> tokens;
      StringUtils::tokenizeMulti(shortName, "::", tokens);
      if (tokens.size() > 1) shortName = tokens.back();
    }

    if (const VectorOfany* const stmts = scope->Stmts()) {
      for (auto init : *stmts) {
        if (init->UhdmType() == uhdmassign_stmt) {
          const expr* const lhs = static_cast<const assign_stmt*>(init)->Lhs();
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

  const UHDM::any* findInClass_defn(const UHDM::any* const object,
                                    const UHDM::class_defn* const scope) {
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
    } else if (const UHDM::any* const actual2 = findInVectorOfAny(
                   name, object, scope->Task_funcs(), scope)) {
      return actual2;
    } else if (const UHDM::any* const actual3 = findInScope(
                   object, static_cast<const UHDM::scope*>(scope))) {
      return actual3;
    } else if (const UHDM::any* const actual4 = findInVectorOfAny(
                   name, object, scope->Constraints(), scope)) {
      return actual4;
    } else if (const UHDM::extends* ext = scope->Extends()) {
      if (const ref_typespec* rt = ext->Class_typespec()) {
        if (const class_typespec* cts = rt->Actual_typespec<class_typespec>()) {
          return findInClass_defn(object, cts->Class_defn());
        }
      }
    }

    return findInScope(object, scope);
  }

  const UHDM::any* findInModule_inst(const UHDM::any* const object,
                                     const UHDM::module_inst* const scope) {
    if (scope == nullptr) return nullptr;
    if (!m_searched.emplace(scope).second) return nullptr;

    std::string_view name = suffixName(object->VpiName());
    if (name == suffixName(scope->VpiName())) {
      return scope;
    } else if (const UHDM::any* const actual1 = findInVectorOfAny(
                   name, object, scope->Interfaces(), scope)) {
      return actual1;
    } else if (const UHDM::any* const actual2 = findInVectorOfAny(
                   name, object, scope->Interface_arrays(), scope)) {
      return actual2;
    }

    return findInInstance(object, scope);
  }

  const UHDM::any* findInDesign(const UHDM::any* const object,
                                const UHDM::design* const scope) {
    if (scope == nullptr) return nullptr;
    if (!m_searched.emplace(scope).second) return nullptr;

    const std::string_view name = object->VpiName();

    if (name == "$root") {
      return scope;
    } else if (scope->VpiName() == name) {
      return scope;
    } else if (const UHDM::any* const actual1 = findInVectorOfAny(
                   name, object, scope->Parameters(), scope)) {
      return actual1;
    } else if (const UHDM::any* const actual2 = findInVectorOfAny(
                   name, object, scope->Param_assigns(), scope)) {
      return actual2;
    } else if (const UHDM::any* const actual = findInVectorOfAny(
                   name, object, scope->AllPackages(), scope)) {
      return actual;
    }

    return nullptr;
  }

  const UHDM::any* getPrefix(const UHDM::any* const object) {
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
                      any_cast<const UHDM::ref_obj*>(previous)) {
                if ((ro1->VpiName() == "this") || (ro1->VpiName() == "super")) {
                  const UHDM::any* prefix = ro1->VpiParent();
                  while (prefix != nullptr) {
                    if (prefix->UhdmType() == UHDM::uhdmclass_defn)
                      return prefix;

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
                          any_cast<const UHDM::class_typespec*>(ts)) {
                    return cts->Class_defn();
                  } else if (const UHDM::struct_typespec* const sts =
                                 any_cast<const UHDM::struct_typespec*>(ts)) {
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

  const UHDM::any* bindObject(const UHDM::any* const object) {
    const ValuedComponentI* component = nullptr;
    const UHDM::instance* scope = nullptr;
    const any* parent = object->VpiParent();

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
        scope = any_cast<const UHDM::instance*>(parent);
      }

      parent = parent->VpiParent();
    }

    for (ComponentMap::const_reference entry : m_componentMap) {
      const DesignComponent* dc =
          valuedcomponenti_cast<const DesignComponent*>(entry.first);
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

  void enterBit_select(const UHDM::bit_select* const object) final {
    if (object->Actual_group() != nullptr) return;

    if (const UHDM::any* actual = bindObject(object)) {
      const_cast<UHDM::bit_select*>(object)->Actual_group(
          const_cast<UHDM::any*>(actual));
    }
  }

  // void enterChandle_var(const UHDM::chandle_var* const object) final {
  //   if (object->Actual_group() != nullptr) return;
  //
  //   if (const UHDM::any* actual = bindObject(object)) {
  //     const_cast<UHDM::chandle_var*>(object)->Actual_group(
  //         const_cast<UHDM::any*>(actual));
  //   }
  // }

  void enterIndexed_part_select(
      const UHDM::indexed_part_select* const object) final {
    if (object->Actual_group() != nullptr) return;

    if (const UHDM::any* actual = bindObject(object)) {
      const_cast<UHDM::indexed_part_select*>(object)->Actual_group(
          const_cast<UHDM::any*>(actual));
    }
  }

  void enterPart_select(const UHDM::part_select* const object) final {
    if (object->Actual_group() != nullptr) return;

    if (const UHDM::any* actual = bindObject(object)) {
      const_cast<UHDM::part_select*>(object)->Actual_group(
          const_cast<UHDM::any*>(actual));
    }
  }

  void enterRef_module(const UHDM::ref_module* const object) final {
    if (object->Actual_group() != nullptr) return;

    if (const UHDM::any* actual = bindObject(object)) {
      const_cast<UHDM::ref_module*>(object)->Actual_group(
          const_cast<UHDM::any*>(actual));
    }
  }

  void enterRef_obj(const UHDM::ref_obj* const object) final {
    if (object->Actual_group() != nullptr) return;

    if (const UHDM::any* actual = bindObject(object)) {
      // Reporting error for $root.
      if ((object->VpiName() == "$root") && (actual->UhdmType() == uhdmdesign))
        return;

      const_cast<UHDM::ref_obj*>(object)->Actual_group(
          const_cast<UHDM::any*>(actual));
    }
  }

  void enterRef_typespec(const UHDM::ref_typespec* const object) final {
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

  void enterClass_defn(const UHDM::class_defn* const object) {
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
          valuedcomponenti_cast<const DesignComponent*>(entry.first);
      if (dc == nullptr) continue;
      if (rt->VpiName() != dc->getName()) continue;

      const ClassDefinition* bdef =
          valuedcomponenti_cast<const ClassDefinition*>(entry.first);

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
          tps->VpiParent(derClsDef);
          rt->Actual_typespec(tps);

          tps->Class_defn(base);
          base->Deriveds(true)->emplace_back(derClsDef);
        }
        break;
      }
    }
  }

  void reportErrors() {
    FileSystem* const fileSystem = FileSystem::getInstance();
    for (auto& [object, scope, component] : m_unbounded) {
      bool reportMissingActual = false;
      if (const UHDM::ref_obj* const ro = any_cast<UHDM::ref_obj>(object)) {
        if (ro->Actual_group() == nullptr) {
          reportMissingActual = true;
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

  bool createDefaultNets() {
    bool result = false;
    Unbounded unbounded(m_unbounded);
    m_unbounded.clear();
    for (auto& [object, scope, component] : unbounded) {
      const ref_obj* ro = any_cast<const ref_obj*>(object);
      if (ro == nullptr) continue;

      if (ro->Actual_group() == nullptr) {
        enterRef_obj(ro);
      }

      if (ro->Actual_group() != nullptr) continue;

      const any* const pro = ro->VpiParent();
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
          m_unbounded.erase(
              std::find_if(m_unbounded.begin(), m_unbounded.end(),
                           [object](Unbounded::value_type& entry) {
                             return std::get<0>(entry) == object;
                           }));
          continue;
        }
      }

      VObjectType defaultNetType = getDefaultNetType(component);
      if ((defaultNetType != VObjectType::slNoType) && (scope != nullptr)) {
        logic_net* net = m_serializer.MakeLogic_net();
        net->VpiName(object->VpiName());
        net->VpiParent(const_cast<UHDM::instance*>(scope));
        net->VpiNetType(UhdmWriter::getVpiNetType(defaultNetType));
        net->VpiLineNo(object->VpiLineNo());
        net->VpiColumnNo(object->VpiColumnNo());
        net->VpiEndLineNo(object->VpiEndLineNo());
        net->VpiEndColumnNo(object->VpiEndColumnNo());
        VectorOfnet* nets = scope->Nets();
        if (nets == nullptr) {
          const_cast<UHDM::instance*>(scope)->Nets(m_serializer.MakeNetVec());
          nets = scope->Nets();
        }
        nets->push_back(net);
        const_cast<UHDM::ref_obj*>(ro)->Actual_group(net);
        result = true;
      }
    }
    return result;
  }

 private:
  const ComponentMap& m_componentMap;
  Serializer& m_serializer;
  SymbolTable* const m_symbolTable = nullptr;
  ErrorContainer* const m_errorContainer = nullptr;
  const bool m_muteStdout = false;

  BaseClassMap m_baseclassMap;
  PrefixStack m_prefixStack;
  DesignStack m_designStack;
  PackageStack m_packageStack;
  Unbounded m_unbounded;
  Searched m_searched;
};

class IntegrityChecker final : public UhdmListener {
 private:
  FileSystem* const m_fileSystem = nullptr;
  SymbolTable* const m_symbolTable = nullptr;
  ErrorContainer* const m_errorContainer = nullptr;

  typedef std::set<UHDM_OBJECT_TYPE> object_type_set_t;
  const object_type_set_t m_acceptedObjectsWithInvalidLocations;

 public:
  IntegrityChecker(FileSystem* fileSystem, SymbolTable* symbolTable,
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

  bool isBuiltPackageOnStack(const UHDM::any* const object) const {
    return ((object->UhdmType() == UHDM::uhdmpackage) &&
            (object->VpiName() == "builtin")) ||
           std::find_if(callstack.crbegin(), callstack.crend(),
                        [](const UHDM::any* const object) {
                          return (object->UhdmType() == UHDM::uhdmpackage) &&
                                 (object->VpiName() == "builtin");
                        }) != callstack.rend();
  }

  template <typename T>
  void reportAmbigiousMembership(const std::vector<T*>* const collection,
                                 const T* const object) const {
    if (object == nullptr) return;
    if ((collection == nullptr) ||
        (std::find(collection->cbegin(), collection->cend(), object) ==
         collection->cend())) {
      Location loc(
          m_fileSystem->toPathId(object->VpiFile(), m_symbolTable),
          object->VpiLineNo(), object->VpiColumnNo(),
          m_symbolTable->registerSymbol(std::to_string(object->UhdmId())));
      Error err(
          ErrorDefinition::INTEGRITY_CHECK_OBJECT_NOT_IN_PARENT_COLLECTION,
          loc);
      m_errorContainer->addError(err);
    }
  }

  template <typename T>
  void reportDuplicates(const UHDM::any* const object,
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
      Error err(ErrorDefinition::INTEGRITY_CHECK_COLLECTION_HAS_DUPLICATES,
                loc);
      m_errorContainer->addError(err);
    }
  }

  void reportInvalidLocation(const UHDM::any* const object) const {
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

    UHDM::UHDM_OBJECT_TYPE oType = object->UhdmType();
    if ((oType == UHDM::uhdmclass_typespec ||
         oType == UHDM::uhdmstruct_typespec) &&
        (object->VpiLineNo() == 0) && (object->VpiEndLineNo() == 0) &&
        (object->VpiColumnNo() == 0) && (object->VpiEndColumnNo() == 0))
      return;

    // Ports and Io_decl are declared in two different ways
    // Io_decl example - TaskDecls
    const std::map<UHDM_OBJECT_TYPE, std::set<UHDM_OBJECT_TYPE>> exclusions{
        {UHDM::uhdmref_typespec, {UHDM::uhdmport, UHDM::uhdmio_decl}},
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
      Error err(ErrorDefinition::
                    INTEGRITY_CHECK_CHILD_NOT_ENTIRELY_IN_PARENT_BOUNDARY,
                loc);
      m_errorContainer->addError(err);
    }
  }

  void reportMissingLocation(const UHDM::any* const object) const {
    if ((object->VpiLineNo() != 0) && (object->VpiColumnNo() != 0) &&
        (object->VpiEndLineNo() != 0) && (object->VpiEndColumnNo() != 0))
      return;

    if (m_acceptedObjectsWithInvalidLocations.find(object->UhdmType()) !=
        m_acceptedObjectsWithInvalidLocations.cend())
      return;

    const UHDM::any* const parent = object->VpiParent();
    const UHDM::any* const grandParent =
        (parent == nullptr) ? parent : parent->VpiParent();

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
    } else if ((object->Cast<variables>() != nullptr) && (parent != nullptr) &&
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

  static bool isImplicitFunctionReturnType(const UHDM::any* const object) {
    if (const UHDM::variables* v = any_cast<UHDM::variables*>(object)) {
      if (const UHDM::function* f =
              any_cast<UHDM::function*>(object->VpiParent())) {
        if ((f->Return() == v) && v->VpiName().empty()) return true;
      }
    }
    return false;
  }

  static std::string_view stripDecorations(std::string_view name) {
    while (!name.empty() && name.back() == ':') name.remove_suffix(1);

    size_t pos1 = name.rfind("::");
    if (pos1 != std::string::npos) name = name.substr(pos1 + 2);

    size_t pos2 = name.rfind('.');
    if (pos2 != std::string::npos) name = name.substr(pos2 + 1);

    size_t pos3 = name.rfind('@');
    if (pos3 != std::string::npos) name = name.substr(pos3 + 1);

    return name;
  }

  static bool areNamedSame(const UHDM::any* const object,
                           const UHDM::any* const actual) {
    std::string_view objectName = stripDecorations(object->VpiName());
    std::string_view actualName = stripDecorations(actual->VpiName());
    return (objectName == actualName);
  }

  void reportInvalidNames(const UHDM::any* const object) const {
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
      }
    }

    if (object->UhdmType() == UHDM::uhdmref_typespec) {
      shouldReport = (object->VpiName() == SymbolTable::getBadSymbol());
      if (const UHDM::any* actual =
              static_cast<const UHDM::ref_typespec*>(object)
                  ->Actual_typespec()) {
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

  void reportInvalidFile(const UHDM::any* const object) const {
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

  void reportNullActual(const UHDM::any* const object) const {
    if (isBuiltPackageOnStack(object)) return;

    bool shouldReport = false;
    switch (object->UhdmType()) {
      case UHDM::uhdmref_obj: {
        shouldReport =
            static_cast<const UHDM::ref_obj*>(object)->Actual_group() ==
            nullptr;
        // Special case for $root.
        shouldReport =
            shouldReport &&
            !((object->VpiName() == "$root") &&
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

  void enterAny(const UHDM::any* const object) final {
    if (isBuiltPackageOnStack(object)) return;

    reportNullActual(object);

    const UHDM::scope* const objectAsScope =
        any_cast<const UHDM::scope*>(object);
    const UHDM::design* const objectAsDesign =
        any_cast<const UHDM::design*>(object);

    if (objectAsScope != nullptr) {
      reportDuplicates(object, objectAsScope->Parameters(), "Parameters");
      reportDuplicates(object, objectAsScope->Param_assigns(), "Param_assigns");
      reportDuplicates(object, objectAsScope->Typespecs(), "Typespecs");
      reportDuplicates(object, objectAsScope->Variables(), "Variables");
      reportDuplicates(object, objectAsScope->Property_decls(),
                       "Property_decls");
      reportDuplicates(object, objectAsScope->Sequence_decls(),
                       "Sequence_decls");
      reportDuplicates(object, objectAsScope->Concurrent_assertions(),
                       "Concurrent_assertions");
      reportDuplicates(object, objectAsScope->Named_events(), "Named_events");
      reportDuplicates(object, objectAsScope->Named_event_arrays(),
                       "Named_event_arrays");
      reportDuplicates(object, objectAsScope->Virtual_interface_vars(),
                       "Virtual_interface_vars");
      reportDuplicates(object, objectAsScope->Logic_vars(), "Logic_vars");
      reportDuplicates(object, objectAsScope->Array_vars(), "Array_vars");
      reportDuplicates(object, objectAsScope->Let_decls(), "Let_decls");
      reportDuplicates(object, objectAsScope->Scopes(), "Scopes");
    } else if (objectAsDesign != nullptr) {
      reportDuplicates(object, objectAsDesign->Typespecs(), "Typespecs");
      reportDuplicates(object, objectAsDesign->Let_decls(), "Let_decls");
      reportDuplicates(object, objectAsDesign->Task_funcs(), "Task_funcs");
      reportDuplicates(object, objectAsDesign->Parameters(), "Parameters");
      reportDuplicates(object, objectAsDesign->Param_assigns(),
                       "Param_assigns");
      return;
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

    const UHDM::scope* const parentAsScope =
        any_cast<const UHDM::scope*>(parent);
    const UHDM::design* const parentAsDesign =
        any_cast<const UHDM::design*>(parent);
    const UHDM::udp_defn* const parentAsUdpDefn =
        any_cast<const UHDM::udp_defn*>(parent);

    const std::set<UHDM_OBJECT_TYPE> allowedScopeChildren{
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
    if (any_cast<begin>(object) != nullptr) {
      expectScope = false;
    }

    const std::set<UHDM_OBJECT_TYPE> allowedDesignChildren{
        UHDM::uhdmpackage,  UHDM::uhdmmodule_inst, UHDM::uhdmclass_defn,
        UHDM::uhdmtypespec, UHDM::uhdmlet_decl,    UHDM::uhdmfunction,
        UHDM::uhdmtask,     UHDM::uhdmparameter,   UHDM::uhdmparam_assign};
    bool expectDesign = (allowedDesignChildren.find(object->UhdmType()) !=
                         allowedDesignChildren.cend());

    const std::set<UHDM_OBJECT_TYPE> allowedUdpChildren{
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
};

static typespec* replace(
    const typespec* orig,
    std::map<const UHDM::typespec*, const UHDM::typespec*>& typespecSwapMap) {
  if (orig && (orig->UhdmType() == uhdmunsupported_typespec)) {
    std::map<const typespec*, const typespec*>::const_iterator itr =
        typespecSwapMap.find(orig);
    if (itr != typespecSwapMap.end()) {
      const typespec* tps = (*itr).second;
      return (typespec*)tps;
    }
  }
  return (typespec*)orig;
}

std::string UhdmWriter::builtinGateName(VObjectType type) {
  std::string modName;
  switch (type) {
    case VObjectType::paNInpGate_And:
      modName = "work@and";
      break;
    case VObjectType::paNInpGate_Or:
      modName = "work@or";
      break;
    case VObjectType::paNInpGate_Nand:
      modName = "work@nand";
      break;
    case VObjectType::paNInpGate_Nor:
      modName = "work@nor";
      break;
    case VObjectType::paNInpGate_Xor:
      modName = "work@xor";
      break;
    case VObjectType::paNInpGate_Xnor:
      modName = "work@xnor";
      break;
    case VObjectType::paNOutGate_Buf:
      modName = "work@buf";
      break;
    case VObjectType::paNOutGate_Not:
      modName = "work@not";
      break;
    case VObjectType::paPassEnSwitch_Tranif0:
      modName = "work@tranif0";
      break;
    case VObjectType::paPassEnSwitch_Tranif1:
      modName = "work@tranif1";
      break;
    case VObjectType::paPassEnSwitch_RTranif1:
      modName = "work@rtranif1";
      break;
    case VObjectType::paPassEnSwitch_RTranif0:
      modName = "work@rtranif0";
      break;
    case VObjectType::paPassSwitch_Tran:
      modName = "work@tran";
      break;
    case VObjectType::paPassSwitch_RTran:
      modName = "work@rtran";
      break;
    case VObjectType::paCmosSwitchType_Cmos:
      modName = "work@cmos";
      break;
    case VObjectType::paCmosSwitchType_RCmos:
      modName = "work@rcmos";
      break;
    case VObjectType::paEnableGateType_Bufif0:
      modName = "work@bufif0";
      break;
    case VObjectType::paEnableGateType_Bufif1:
      modName = "work@bufif1";
      break;
    case VObjectType::paEnableGateType_Notif0:
      modName = "work@notif0";
      break;
    case VObjectType::paEnableGateType_Notif1:
      modName = "work@notif1";
      break;
    case VObjectType::paMosSwitchType_NMos:
      modName = "work@nmos";
      break;
    case VObjectType::paMosSwitchType_PMos:
      modName = "work@pmos";
      break;
    case VObjectType::paMosSwitchType_RNMos:
      modName = "work@rnmos";
      break;
    case VObjectType::paMosSwitchType_RPMos:
      modName = "work@rpmos";
      break;
    case VObjectType::paPULLUP:
      modName = "work@pullup";
      break;
    case VObjectType::paPULLDOWN:
      modName = "work@pulldown";
      break;
    default:
      modName = "work@UnsupportedPrimitive";
      break;
  }
  return modName;
}

UhdmWriter::UhdmWriter(CompileDesign* compiler, Design* design)
    : m_compileDesign(compiler), m_design(design) {
  m_helper.seterrorReporting(
      m_compileDesign->getCompiler()->getErrorContainer(),
      m_compileDesign->getCompiler()->getSymbolTable());
}

uint32_t UhdmWriter::getStrengthType(VObjectType type) {
  switch (type) {
    case VObjectType::paSUPPLY0:
      return vpiSupply0;
    case VObjectType::paSUPPLY1:
      return vpiSupply1;
    case VObjectType::paSTRONG0:
      return vpiStrongDrive;
    case VObjectType::paSTRONG1:
      return vpiStrongDrive;
    case VObjectType::paPULL0:
      return vpiPullDrive;
    case VObjectType::paPULL1:
      return vpiPullDrive;
    case VObjectType::paWEAK0:
      return vpiWeakDrive;
    case VObjectType::paWEAK1:
      return vpiWeakDrive;
    case VObjectType::paHIGHZ0:
      return vpiHighZ;
    case VObjectType::paHIGHZ1:
      return vpiHighZ;
    default:
      return 0;
  }
}

uint32_t UhdmWriter::getVpiOpType(VObjectType type) {
  switch (type) {
    case VObjectType::paBinOp_Plus:
      return vpiAddOp;
    case VObjectType::paBinOp_Minus:
      return vpiSubOp;
    case VObjectType::paBinOp_Mult:
      return vpiMultOp;
    case VObjectType::paBinOp_MultMult:
      return vpiPowerOp;
    case VObjectType::paBinOp_Div:
      return vpiDivOp;
    case VObjectType::paBinOp_Great:
      return vpiGtOp;
    case VObjectType::paBinOp_GreatEqual:
      return vpiGeOp;
    case VObjectType::paBinOp_Less:
      return vpiLtOp;
    case VObjectType::paBinOp_Imply:
      return vpiImplyOp;
    case VObjectType::paBinOp_Equivalence:
      return vpiEqOp;
    case VObjectType::paBinOp_LessEqual:
      return vpiLeOp;
    case VObjectType::paBinOp_Equiv:
      return vpiEqOp;
    case VObjectType::paBinOp_Not:
    case VObjectType::paNOT:
      return vpiNeqOp;
    case VObjectType::paBinOp_Percent:
      return vpiModOp;
    case VObjectType::paBinOp_LogicAnd:
      return vpiLogAndOp;
    case VObjectType::paBinOp_LogicOr:
      return vpiLogOrOp;
    case VObjectType::paBinOp_BitwAnd:
      return vpiBitAndOp;
    case VObjectType::paBinOp_BitwOr:
      return vpiBitOrOp;
    case VObjectType::paBinOp_BitwXor:
      return vpiBitXorOp;
    case VObjectType::paBinOp_ReductXnor1:
    case VObjectType::paBinOp_ReductXnor2:
    case VObjectType::paBinModOp_ReductXnor1:
    case VObjectType::paBinModOp_ReductXnor2:
      return vpiBitXNorOp;
    case VObjectType::paBinOp_ReductNand:
      return vpiUnaryNandOp;
    case VObjectType::paBinOp_ReductNor:
      return vpiUnaryNorOp;
    case VObjectType::paUnary_Plus:
      return vpiPlusOp;
    case VObjectType::paUnary_Minus:
      return vpiMinusOp;
    case VObjectType::paUnary_Not:
      return vpiNotOp;
    case VObjectType::paUnary_Tilda:
      return vpiBitNegOp;
    case VObjectType::paUnary_BitwAnd:
      return vpiUnaryAndOp;
    case VObjectType::paUnary_BitwOr:
      return vpiUnaryOrOp;
    case VObjectType::paUnary_BitwXor:
      return vpiUnaryXorOp;
    case VObjectType::paUnary_ReductNand:
      return vpiUnaryNandOp;
    case VObjectType::paUnary_ReductNor:
      return vpiUnaryNorOp;
    case VObjectType::paUnary_ReductXnor1:
    case VObjectType::paUnary_ReductXnor2:
      return vpiUnaryXNorOp;
    case VObjectType::paBinOp_ShiftLeft:
      return vpiLShiftOp;
    case VObjectType::paBinOp_ShiftRight:
      return vpiRShiftOp;
    case VObjectType::paBinOp_ArithShiftLeft:
      return vpiArithLShiftOp;
    case VObjectType::paBinOp_ArithShiftRight:
      return vpiArithRShiftOp;
    case VObjectType::paIncDec_PlusPlus:
      return vpiPostIncOp;
    case VObjectType::paIncDec_MinusMinus:
      return vpiPostDecOp;
    case VObjectType::paConditional_operator:
    case VObjectType::paQMARK:
      return vpiConditionOp;
    case VObjectType::paINSIDE:
    case VObjectType::paOpen_range_list:
      return vpiInsideOp;
    case VObjectType::paBinOp_FourStateLogicEqual:
      return vpiCaseEqOp;
    case VObjectType::paBinOp_FourStateLogicNotEqual:
      return vpiCaseNeqOp;
    case VObjectType::paAssignOp_Assign:
      return vpiAssignmentOp;
    case VObjectType::paAssignOp_Add:
      return vpiAddOp;
    case VObjectType::paAssignOp_Sub:
      return vpiSubOp;
    case VObjectType::paAssignOp_Mult:
      return vpiMultOp;
    case VObjectType::paAssignOp_Div:
      return vpiDivOp;
    case VObjectType::paAssignOp_Modulo:
      return vpiModOp;
    case VObjectType::paAssignOp_BitwAnd:
      return vpiBitAndOp;
    case VObjectType::paAssignOp_BitwOr:
      return vpiBitOrOp;
    case VObjectType::paAssignOp_BitwXor:
      return vpiBitXorOp;
    case VObjectType::paAssignOp_BitwLeftShift:
      return vpiLShiftOp;
    case VObjectType::paAssignOp_BitwRightShift:
      return vpiRShiftOp;
    case VObjectType::paAssignOp_ArithShiftLeft:
      return vpiArithLShiftOp;
    case VObjectType::paAssignOp_ArithShiftRight:
      return vpiArithRShiftOp;
    case VObjectType::paMatches:
      return vpiMatchOp;
    case VObjectType::paBinOp_WildcardEqual:
    case VObjectType::paBinOp_WildEqual:
      return vpiWildEqOp;
    case VObjectType::paBinOp_WildcardNotEqual:
    case VObjectType::paBinOp_WildNotEqual:
      return vpiWildNeqOp;
    case VObjectType::paIFF:
      return vpiIffOp;
    case VObjectType::paOR:
      return vpiLogOrOp;
    case VObjectType::paAND:
      return vpiLogAndOp;
    case VObjectType::paNON_OVERLAP_IMPLY:
      return vpiNonOverlapImplyOp;
    case VObjectType::paOVERLAP_IMPLY:
      return vpiOverlapImplyOp;
    case VObjectType::paOVERLAPPED:
      return vpiOverlapFollowedByOp;
    case VObjectType::paNONOVERLAPPED:
      return vpiNonOverlapFollowedByOp;
    case VObjectType::paUNTIL:
      return vpiUntilOp;
    case VObjectType::paS_UNTIL:
      return vpiUntilOp;
    case VObjectType::paUNTIL_WITH:
      return vpiUntilWithOp;
    case VObjectType::paS_UNTIL_WITH:
      return vpiUntilWithOp;
    case VObjectType::paIMPLIES:
      return vpiImpliesOp;
    case VObjectType::paCycle_delay_range:
      return vpiCycleDelayOp;
    case VObjectType::paConsecutive_repetition:
      return vpiConsecutiveRepeatOp;
    case VObjectType::paNon_consecutive_repetition:
      return vpiRepeatOp;
    case VObjectType::paGoto_repetition:
      return vpiGotoRepeatOp;
    case VObjectType::paTHROUGHOUT:
      return vpiThroughoutOp;
    case VObjectType::paWITHIN:
      return vpiWithinOp;
    case VObjectType::paINTERSECT:
      return vpiIntersectOp;
    case VObjectType::paFIRST_MATCH:
      return vpiFirstMatchOp;
    case VObjectType::paSTRONG:
      return vpiOpStrong;
    case VObjectType::paACCEPT_ON:
      return vpiAcceptOnOp;
    case VObjectType::paSYNC_ACCEPT_ON:
      return vpiSyncAcceptOnOp;
    case VObjectType::paREJECT_ON:
      return vpiRejectOnOp;
    case VObjectType::paSYNC_REJECT_ON:
      return vpiSyncRejectOnOp;
    case VObjectType::paNEXTTIME:
      return vpiNexttimeOp;
    case VObjectType::paS_NEXTTIME:
      return vpiNexttimeOp;
    case VObjectType::paALWAYS:
      return vpiAlwaysOp;
    case VObjectType::paEVENTUALLY:
      return vpiEventuallyOp;
    default:
      return 0;
  }
}

bool isMultidimensional(const UHDM::typespec* ts) {
  bool isMultiDimension = false;
  if (ts) {
    if (ts->UhdmType() == uhdmlogic_typespec) {
      logic_typespec* lts = (logic_typespec*)ts;
      if (lts->Ranges() && lts->Ranges()->size() > 1) isMultiDimension = true;
    } else if (ts->UhdmType() == uhdmarray_typespec) {
      array_typespec* lts = (array_typespec*)ts;
      if (lts->Ranges() && lts->Ranges()->size() > 1) isMultiDimension = true;
    } else if (ts->UhdmType() == uhdmpacked_array_typespec) {
      packed_array_typespec* lts = (packed_array_typespec*)ts;
      if (lts->Ranges() && lts->Ranges()->size() > 1) isMultiDimension = true;
    } else if (ts->UhdmType() == uhdmbit_typespec) {
      bit_typespec* lts = (bit_typespec*)ts;
      if (lts->Ranges() && lts->Ranges()->size() > 1) isMultiDimension = true;
    }
  }
  return isMultiDimension;
}

bool writeElabParameters(Serializer& s, ModuleInstance* instance,
                         UHDM::scope* m, ExprBuilder& exprBuilder) {
  Netlist* netlist = instance->getNetlist();
  DesignComponent* mod = instance->getDefinition();

  std::map<std::string_view, any*> paramSet;
  if (netlist->param_assigns()) {
    VectorOfany* params = m->Parameters();
    if (params == nullptr) {
      params = s.MakeAnyVec();
      m->Parameters(params);
    }
    m->Param_assigns(netlist->param_assigns());
  }

  if (mod) {
    VectorOfany* params = m->Parameters();
    if (params == nullptr) {
      params = s.MakeAnyVec();
      m->Parameters(params);
    }
    VectorOfany* orig_params = mod->getParameters();
    if (orig_params) {
      for (auto orig : *orig_params) {
        const std::string_view name = orig->VpiName();
        bool pushed = false;
        // Specifc handling of type parameters
        if (orig->UhdmType() == uhdmtype_parameter) {
          for (auto p : instance->getTypeParams()) {
            if (p->getName() == orig->VpiName()) {
              any* uparam = p->getUhdmParam();
              if (uparam) {
                uparam->VpiParent(m);
                for (VectorOfany::iterator itr = params->begin();
                     itr != params->end(); itr++) {
                  if ((*itr)->VpiName() == p->getName()) {
                    params->erase(itr);
                    break;
                  }
                }
                params->push_back(uparam);
                pushed = true;
              }
              break;
            }
          }
          if (!pushed) {
            // These point to the sole copy (unelaborated)
            for (VectorOfany::iterator itr = params->begin();
                 itr != params->end(); itr++) {
              if ((*itr)->VpiName() == orig->VpiName()) {
                params->erase(itr);
                break;
              }
            }
            params->push_back(orig);
          }
        } else {
          // Regular param
          ElaboratorContext elaboratorContext(&s, false, true);
          any* pclone = UHDM::clone_tree(orig, &elaboratorContext);
          pclone->VpiParent(m);
          paramSet.emplace(name, pclone);

          /*

            Keep the value of the parameter used during definition. The
          param_assign contains the actual value useful for elaboration

          const typespec* ts = ((parameter*)pclone)->Typespec();
          bool multi = isMultidimensional(ts);
          if (((parameter*)pclone)->Ranges() &&
          ((parameter*)pclone)->Ranges()->size() > 1) multi = true;

          if (instance->getComplexValue(name)) {
          } else {
            Value* val = instance->getValue(name, exprBuilder);
            if (val && val->isValid() && (!multi)) {
              ((parameter*)pclone)->VpiValue(val->uhdmValue());
            }
          }
          */
          params->push_back(pclone);
        }
      }
    }
  }

  if (netlist->param_assigns()) {
    VectorOfany* params = m->Parameters();
    if (params == nullptr) {
      params = s.MakeAnyVec();
      m->Parameters(params);
    }
    for (auto p : *netlist->param_assigns()) {
      bool found = false;
      for (auto pt : *params) {
        if (pt->VpiName() == p->Lhs()->VpiName()) {
          found = true;
          break;
        }
      }
      if (!found) params->push_back(p->Lhs());
    }
  }

  if (netlist->param_assigns()) {
    for (auto ps : *m->Param_assigns()) {
      ps->VpiParent(m);
      const std::string_view name = ps->Lhs()->VpiName();
      auto itr = paramSet.find(name);
      if (itr != paramSet.end()) {
        ps->Lhs((*itr).second);
      }
    }
  }
  return true;
}

uint32_t UhdmWriter::getVpiDirection(VObjectType type) {
  uint32_t direction = vpiInout;
  if (type == VObjectType::paPortDir_Inp ||
      type == VObjectType::paTfPortDir_Inp)
    direction = vpiInput;
  else if (type == VObjectType::paPortDir_Out ||
           type == VObjectType::paTfPortDir_Out)
    direction = vpiOutput;
  else if (type == VObjectType::paPortDir_Inout ||
           type == VObjectType::paTfPortDir_Inout)
    direction = vpiInout;
  else if (type == VObjectType::paTfPortDir_Ref ||
           type == VObjectType::paTfPortDir_ConstRef)
    direction = vpiRef;
  return direction;
}

uint32_t UhdmWriter::getVpiNetType(VObjectType type) {
  uint32_t nettype = 0;
  switch (type) {
    case VObjectType::paNetType_Wire:
      nettype = vpiWire;
      break;
    case VObjectType::paIntVec_TypeReg:
      nettype = vpiReg;
      break;
    case VObjectType::paNetType_Supply0:
      nettype = vpiSupply0;
      break;
    case VObjectType::paNetType_Supply1:
      nettype = vpiSupply1;
      break;
    case VObjectType::paIntVec_TypeLogic:
      nettype = vpiLogicNet;
      break;
    case VObjectType::paNetType_Wand:
      nettype = vpiWand;
      break;
    case VObjectType::paNetType_Wor:
      nettype = vpiWor;
      break;
    case VObjectType::paNetType_Tri:
      nettype = vpiTri;
      break;
    case VObjectType::paNetType_Tri0:
      nettype = vpiTri0;
      break;
    case VObjectType::paNetType_Tri1:
      nettype = vpiTri1;
      break;
    case VObjectType::paNetType_TriReg:
      nettype = vpiTriReg;
      break;
    case VObjectType::paNetType_TriAnd:
      nettype = vpiTriAnd;
      break;
    case VObjectType::paNetType_TriOr:
      nettype = vpiTriOr;
      break;
    case VObjectType::paNetType_Uwire:
      nettype = vpiUwire;
      break;
    case VObjectType::paImplicit_data_type:
    case VObjectType::paSigning_Signed:
    case VObjectType::paPacked_dimension:
    case VObjectType::paSigning_Unsigned:
      nettype = vpiNone;
      break;
    default:
      break;
  }
  return nettype;
}

void UhdmWriter::writePorts(std::vector<Signal*>& orig_ports, BaseClass* parent,
                            VectorOfport* dest_ports, VectorOfnet* dest_nets,
                            Serializer& s, ModPortMap& modPortMap,
                            SignalBaseClassMap& signalBaseMap,
                            SignalMap& signalMap, ModuleInstance* instance,
                            DesignComponent* mod) {
  int32_t lastPortDirection = vpiInout;
  for (Signal* orig_port : orig_ports) {
    port* dest_port = s.MakePort();
    signalBaseMap.emplace(orig_port, dest_port);
    signalMap.emplace(orig_port->getName(), orig_port);

    if (orig_port->attributes()) {
      dest_port->Attributes(orig_port->attributes());
      for (auto ats : *orig_port->attributes()) {
        ats->VpiParent(dest_port);
      }
    }

    const FileContent* fC = orig_port->getFileContent();
    if (fC->Type(orig_port->getNameId()) == VObjectType::slStringConst)
      dest_port->VpiName(orig_port->getName());
    if (orig_port->getDirection() != VObjectType::slNoType)
      lastPortDirection =
          UhdmWriter::getVpiDirection(orig_port->getDirection());
    dest_port->VpiDirection(lastPortDirection);
    if (const FileContent* const fC = orig_port->getFileContent()) {
      fC->populateCoreMembers(orig_port->getNodeId(), orig_port->getNameId(),
                              dest_port);
    }
    dest_port->VpiParent(parent);
    if (ModPort* orig_modport = orig_port->getModPort()) {
      ref_obj* ref = s.MakeRef_obj();
      ref->VpiName(orig_port->getName());
      ref->VpiParent(dest_port);
      dest_port->Low_conn(ref);
      std::map<ModPort*, modport*>::iterator itr =
          modPortMap.find(orig_modport);
      if (itr != modPortMap.end()) {
        ref->Actual_group((*itr).second);
      }
    } else if (ModuleDefinition* orig_interf = orig_port->getInterfaceDef()) {
      ref_obj* ref = s.MakeRef_obj();
      ref->VpiName(orig_port->getName());
      ref->VpiParent(dest_port);
      dest_port->Low_conn(ref);
      const auto& found = m_componentMap.find(orig_interf);
      if (found != m_componentMap.end()) {
        ref->Actual_group(found->second);
      }
    }
    if (NodeId defId = orig_port->getDefaultValue()) {
      if (any* exp = m_helper.compileExpression(mod, fC, defId, m_compileDesign,
                                                Reduce::No, dest_port, instance,
                                                false)) {
        dest_port->High_conn(exp);
      }
    }
    if (orig_port->getTypeSpecId() && mod) {
      if (NodeId unpackedDimensions = orig_port->getUnpackedDimension()) {
        NodeId nameId = orig_port->getNameId();
        NodeId packedDimensions = orig_port->getPackedDimension();
        int32_t unpackedSize = 0;
        const FileContent* fC = orig_port->getFileContent();
        if (std::vector<UHDM::range*>* ranges = m_helper.compileRanges(
                mod, fC, unpackedDimensions, m_compileDesign, Reduce::No,
                nullptr, instance, unpackedSize, false)) {
          array_typespec* array_ts = s.MakeArray_typespec();
          array_ts->Ranges(ranges);
          array_ts->VpiParent(dest_port);
          fC->populateCoreMembers(unpackedDimensions, unpackedDimensions,
                                  array_ts);
          for (range* r : *ranges) {
            r->VpiParent(array_ts);
            const expr* rrange = r->Right_expr();
            if (rrange->VpiValue() == "STRING:associative") {
              array_ts->VpiArrayType(vpiAssocArray);
              if (const ref_typespec* rt = rrange->Typespec()) {
                if (const typespec* ag = rt->Actual_typespec()) {
                  ref_typespec* cro = s.MakeRef_typespec();
                  cro->VpiParent(array_ts);
                  cro->Actual_typespec(const_cast<typespec*>(ag));
                  array_ts->Index_typespec(cro);
                }
              }
            } else if (rrange->VpiValue() == "STRING:unsized") {
              array_ts->VpiArrayType(vpiDynamicArray);
            } else if (rrange->VpiValue() == "STRING:$") {
              array_ts->VpiArrayType(vpiQueueArray);
            } else {
              array_ts->VpiArrayType(vpiStaticArray);
            }
          }
          if (dest_port->Typespec() == nullptr) {
            ref_typespec* dest_port_rt = s.MakeRef_typespec();
            dest_port_rt->VpiParent(dest_port);
            fC->populateCoreMembers(
                nameId, packedDimensions ? packedDimensions : nameId,
                dest_port_rt);
            dest_port->Typespec(dest_port_rt);
          }
          dest_port->Typespec()->Actual_typespec(array_ts);

          if (typespec* ts = m_helper.compileTypespec(
                  mod, fC, orig_port->getTypeSpecId(), m_compileDesign,
                  Reduce::No, array_ts, nullptr, true)) {
            if (array_ts->Elem_typespec() == nullptr) {
              ref_typespec* array_ts_rt = s.MakeRef_typespec();
              array_ts_rt->VpiParent(array_ts);
              fC->populateCoreMembers(
                  nameId, packedDimensions ? packedDimensions : nameId,
                  array_ts_rt);
              array_ts_rt->VpiName(ts->VpiName());
              array_ts->Elem_typespec(array_ts_rt);
            }
            array_ts->Elem_typespec()->Actual_typespec(ts);
          }
        }
      } else if (typespec* ts = m_helper.compileTypespec(
                     mod, fC, orig_port->getTypeSpecId(), m_compileDesign,
                     Reduce::No, dest_port, nullptr, true)) {
        if (dest_port->Typespec() == nullptr) {
          ref_typespec* dest_port_rt = s.MakeRef_typespec();
          dest_port_rt->VpiName(ts->VpiName());
          dest_port_rt->VpiParent(dest_port);
          dest_port->Typespec(dest_port_rt);
          fC->populateCoreMembers(orig_port->getTypeSpecId(),
                                  orig_port->getTypeSpecId(), dest_port_rt);
        }
        dest_port->Typespec()->Actual_typespec(ts);
      }
    }
    dest_ports->push_back(dest_port);
  }
}

void UhdmWriter::writeDataTypes(const DesignComponent::DataTypeMap& datatypeMap,
                                BaseClass* parent,
                                VectorOftypespec* dest_typespecs, Serializer& s,
                                bool setParent) {
  std::set<uint64_t> ids;
  for (const typespec* t : *dest_typespecs) ids.emplace(t->UhdmId());
  for (const auto& entry : datatypeMap) {
    const DataType* dtype = entry.second;
    if (dtype->getCategory() == DataType::Category::REF) {
      dtype = dtype->getDefinition();
    }
    if (dtype->getCategory() == DataType::Category::TYPEDEF) {
      if (dtype->getTypespec() == nullptr) dtype = dtype->getDefinition();
    }
    typespec* tps = dtype->getTypespec();
    tps = replace(tps, m_compileDesign->getSwapedObjects());
    if (parent->UhdmType() == uhdmpackage) {
      if (tps && (tps->VpiName().find("::") == std::string::npos)) {
        const std::string newName =
            StrCat(parent->VpiName(), "::", tps->VpiName());
        tps->VpiName(newName);
      }
    }

    if (tps) {
      if (!tps->Instance()) {
        if (parent->UhdmType() != uhdmclass_defn)
          tps->Instance((instance*)parent);
      }
      if (setParent && (tps->VpiParent() == nullptr)) {
        tps->VpiParent(parent);
        ids.emplace(tps->UhdmId());
      } else if (ids.emplace(tps->UhdmId()).second) {
        dest_typespecs->push_back(tps);
      }
    }
  }
}

void UhdmWriter::writeNets(DesignComponent* mod,
                           std::vector<Signal*>& orig_nets, BaseClass* parent,
                           VectorOfnet* dest_nets, Serializer& s,
                           SignalBaseClassMap& signalBaseMap,
                           SignalMap& signalMap, SignalMap& portMap,
                           ModuleInstance* instance /* = nullptr */) {
  for (auto& orig_net : orig_nets) {
    net* dest_net = nullptr;
    if (instance) {
      for (net* net : *instance->getNetlist()->nets()) {
        UhdmWriter::SignalMap::iterator itr = signalMap.find(net->VpiName());
        if (itr == signalMap.end()) {
          if (net->VpiName() == orig_net->getName()) {
            dest_net = net;
            break;
          }
        }
      }
    } else {
      dest_net = s.MakeLogic_net();
    }
    if (dest_net) {
      const FileContent* fC = orig_net->getFileContent();
      if (fC->Type(orig_net->getNameId()) == VObjectType::slStringConst) {
        auto portItr = portMap.find(orig_net->getName());
        if (portItr != portMap.end()) {
          Signal* sig = (*portItr).second;
          if (sig) {
            UhdmWriter::SignalBaseClassMap::iterator itr =
                signalBaseMap.find(sig);
            if (itr != signalBaseMap.end()) {
              port* p = (port*)((*itr).second);
              NodeId nameId = orig_net->getNameId();
              if (p->Low_conn() == nullptr) {
                ref_obj* ref = s.MakeRef_obj();
                ref->VpiName(p->VpiName());
                ref->Actual_group(dest_net);
                ref->VpiParent(p);
                p->Low_conn(ref);
                fC->populateCoreMembers(nameId, nameId, ref);
              } else if (p->Low_conn()->UhdmType() == uhdmref_obj) {
                ref_obj* ref = (ref_obj*)p->Low_conn();
                ref->VpiName(p->VpiName());
                if (ref->VpiLineNo() == 0) {
                  fC->populateCoreMembers(nameId, nameId, ref);
                }
                if (ref->Actual_group() == nullptr) {
                  ref->Actual_group(dest_net);
                }
                ref->VpiParent(p);
              }
            }
          }
        } else if (dest_net->Typespec() == nullptr) {
          // compileTypespec function need to account for range
          // location information if there is any in the typespec.
          if (orig_net->getTypeSpecId()) {
            if (typespec* ts = m_helper.compileTypespec(
                    mod, fC, orig_net->getTypeSpecId(), m_compileDesign,
                    Reduce::No, nullptr, nullptr, true)) {
              ref_typespec* rt = s.MakeRef_typespec();
              rt->VpiName(ts->VpiName());
              rt->VpiParent(dest_net);
              rt->Actual_typespec(ts);
              dest_net->Typespec(rt);
              fC->populateCoreMembers(orig_net->getTypeSpecId(),
                                      orig_net->getTypeSpecId(), rt);
              NodeId dimensions = orig_net->getUnpackedDimension();
              if (!dimensions) dimensions = orig_net->getPackedDimension();
              if (dimensions) {
                fC->populateCoreMembers(InvalidNodeId, dimensions, ts);
              }
            }
          }
        }
        signalBaseMap.emplace(orig_net, dest_net);
        signalMap.emplace(orig_net->getName(), orig_net);
        dest_net->VpiName(orig_net->getName());
        if (const FileContent* const fC = orig_net->getFileContent()) {
          fC->populateCoreMembers(orig_net->getNameId(), orig_net->getNameId(),
                                  dest_net);
        }
        dest_net->VpiNetType(UhdmWriter::getVpiNetType(orig_net->getType()));
        dest_net->VpiParent(parent);
        dest_nets->push_back(dest_net);
      }
    }
  }
}

void mapLowConns(std::vector<Signal*>& orig_ports, Serializer& s,
                 UhdmWriter::SignalBaseClassMap& signalBaseMap) {
  for (Signal* orig_port : orig_ports) {
    if (Signal* lowconn = orig_port->getLowConn()) {
      UhdmWriter::SignalBaseClassMap::iterator itrlow =
          signalBaseMap.find(lowconn);
      if (itrlow != signalBaseMap.end()) {
        UhdmWriter::SignalBaseClassMap::iterator itrport =
            signalBaseMap.find(orig_port);
        if (itrport != signalBaseMap.end()) {
          ref_obj* ref = s.MakeRef_obj();
          ((port*)itrport->second)->Low_conn(ref);
          ref->VpiParent(itrport->second);
          ref->Actual_group(itrlow->second);
          ref->VpiName(orig_port->getName());
          orig_port->getFileContent()->populateCoreMembers(
              orig_port->getNodeId(), orig_port->getNodeId(), ref);
        }
      }
    }
  }
}

void UhdmWriter::writeClass(ClassDefinition* classDef,
                            VectorOfclass_defn* dest_classes, Serializer& s,
                            BaseClass* parent) {
  if (!classDef->getFileContents().empty() &&
      classDef->getType() == VObjectType::paClass_declaration) {
    const FileContent* fC = classDef->getFileContents()[0];
    class_defn* c = classDef->getUhdmScope<class_defn>();
    m_componentMap.emplace(classDef, c);
    c->VpiParent(parent);
    c->VpiEndLabel(classDef->getEndLabel());
    c->Task_func_decls(classDef->getTask_func_decls());

    // Typepecs
    VectorOftypespec* typespecs = c->Typespecs();
    if (typespecs == nullptr) {
      typespecs = s.MakeTypespecVec();
      c->Typespecs(typespecs);
    }
    writeDataTypes(classDef->getDataTypeMap(), c, typespecs, s, true);

    // Variables
    // Already bound in TestbenchElaboration

    // Extends, fix late binding
    if (const extends* ext = c->Extends()) {
      if (const ref_typespec* ext_rt = ext->Class_typespec()) {
        if (const class_typespec* tps =
                ext_rt->Actual_typespec<UHDM::class_typespec>()) {
          if (tps->Class_defn() == nullptr) {
            const std::string_view tpsName = tps->VpiName();
            if (c->Parameters()) {
              for (auto ps : *c->Parameters()) {
                if (ps->VpiName() == tpsName) {
                  if (ps->UhdmType() == uhdmtype_parameter) {
                    type_parameter* tp = (type_parameter*)ps;
                    if (const UHDM::ref_typespec* tp_rt = tp->Typespec()) {
                      if (const class_typespec* cptp =
                              tp_rt->Actual_typespec<UHDM::class_typespec>()) {
                        ((class_typespec*)tps)
                            ->Class_defn((class_defn*)cptp->Class_defn());
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

    // Param_assigns
    if (classDef->getParam_assigns()) {
      c->Param_assigns(classDef->getParam_assigns());
      for (auto ps : *c->Param_assigns()) {
        ps->VpiParent(c);
      }
    }
    c->VpiParent(parent);
    dest_classes->push_back(c);
    const std::string_view name = classDef->getName();
    if (c->VpiName().empty()) c->VpiName(name);
    if (c->VpiFullName().empty()) c->VpiFullName(name);
    if (classDef->Attributes() != nullptr) {
      c->Attributes(classDef->Attributes());
      for (auto a : *c->Attributes()) {
        a->VpiParent(c);
      }
    }
    if (fC) {
      // Builtin classes have no file
      const NodeId modId = classDef->getNodeIds()[0];
      const NodeId startId = fC->sl_collect(modId, VObjectType::paCLASS);
      fC->populateCoreMembers(startId, modId, c);
    }
    // Activate when hier_path is better supported
    // lateTypedefBinding(s, classDef, c);
    // lateBinding(s, classDef, c);

    for (auto& nested : classDef->getClassMap()) {
      ClassDefinition* c_nested = nested.second;
      VectorOfclass_defn* dest_classes = s.MakeClass_defnVec();
      writeClass(c_nested, dest_classes, s, c);
    }
  }
}

void UhdmWriter::writeClasses(ClassNameClassDefinitionMultiMap& orig_classes,
                              VectorOfclass_defn* dest_classes, Serializer& s,
                              BaseClass* parent) {
  for (auto& orig_class : orig_classes) {
    ClassDefinition* classDef = orig_class.second;
    writeClass(classDef, dest_classes, s, parent);
  }
}

void UhdmWriter::writeVariables(const DesignComponent::VariableMap& orig_vars,
                                BaseClass* parent, VectorOfvariables* dest_vars,
                                Serializer& s) {
  for (auto& orig_var : orig_vars) {
    Variable* var = orig_var.second;
    const DataType* dtype = var->getDataType();
    const ClassDefinition* classdef =
        datatype_cast<const ClassDefinition*>(dtype);
    if (classdef) {
      class_var* cvar = s.MakeClass_var();
      cvar->VpiName(var->getName());
      var->getFileContent()->populateCoreMembers(var->getNodeId(),
                                                 var->getNodeId(), cvar);
      cvar->VpiParent(parent);
      const auto& found = m_componentMap.find(classdef);
      if (found != m_componentMap.end()) {
        // TODO: Bind Class type,
        // class_var -> class_typespec -> class_defn
      }
      dest_vars->push_back(cvar);
    }
  }
}

class ReInstanceTypespec : public VpiListener {
 public:
  explicit ReInstanceTypespec(package* p) : m_package(p) {}
  ~ReInstanceTypespec() override = default;

  void leaveAny(const any* object, vpiHandle handle) final {
    if (any_cast<const typespec*>(object) != nullptr) {
      if ((object->UhdmType() != uhdmevent_typespec) &&
          (object->UhdmType() != uhdmimport_typespec) &&
          (object->UhdmType() != uhdmtype_parameter)) {
        reInstance(object);
      }
    }
  }

  void leaveFunction(const function* object, vpiHandle handle) final {
    reInstance(object);
  }
  void leaveTask(const task* object, vpiHandle handle) final {
    reInstance(object);
  }
  void reInstance(const any* cobject) {
    if (cobject == nullptr) return;
    any* object = (any*)cobject;
    const instance* inst = nullptr;
    if (typespec* tps = any_cast<typespec*>(object)) {
      inst = (instance*)tps->Instance();
    } else if (function* tps = any_cast<function*>(object)) {
      inst = (instance*)tps->Instance();
    } else if (task* tps = any_cast<task*>(object)) {
      inst = (instance*)tps->Instance();
    }
    if (inst) {
      const std::string_view name = inst->VpiName();
      design* d = (design*)m_package->VpiParent();
      if (d->AllPackages() != nullptr) {
        for (auto pack : *d->AllPackages()) {
          if (pack->VpiName() == name) {
            if (typespec* tps = any_cast<typespec*>(object)) {
              tps->Instance(pack);
              if (const enum_typespec* et =
                      any_cast<const enum_typespec*>(cobject)) {
                reInstance(et->Base_typespec());
              }
            } else if (function* tps = any_cast<function*>(object)) {
              tps->Instance(pack);
            } else if (task* tps = any_cast<task*>(object)) {
              tps->Instance(pack);
            }
            break;
          }
        }
      }
    }
  }

 private:
  package* m_package = nullptr;
};

// Non-elaborated package typespec Instance relation need to point to
// non-elablarated package
void reInstanceTypespec(Serializer& serializer, any* root, package* p) {
  ReInstanceTypespec* listener = new ReInstanceTypespec(p);
  vpiHandle handle = serializer.MakeUhdmHandle(root->UhdmType(), root);
  listener->listenAny(handle);
  vpi_release_handle(handle);
  delete listener;
}

void UhdmWriter::writePackage(Package* pack, package* p, Serializer& s,
                              bool elaborated) {
  const ScopedScope scopedScope(p);

  p->VpiEndLabel(pack->getEndLabel());
  p->VpiFullName(StrCat(pack->getName(), "::"));
  VectorOfclass_defn* dest_classes = nullptr;

  // Typepecs
  if (elaborated) {
    VectorOftypespec* typespecs = p->Typespecs();
    if (typespecs == nullptr) {
      typespecs = s.MakeTypespecVec();
      p->Typespecs(typespecs);
    }

    for (auto tp : *typespecs) {
      tp->Instance(p);
    }
  }
  // Classes
  ClassNameClassDefinitionMultiMap& orig_classes = pack->getClassDefinitions();
  dest_classes = s.MakeClass_defnVec();
  writeClasses(orig_classes, dest_classes, s, p);
  p->Class_defns(dest_classes);
  // Parameters
  if (p->Parameters()) {
    for (auto ps : *p->Parameters()) {
      if (ps->UhdmType() == uhdmparameter) {
        ((parameter*)ps)
            ->VpiFullName(StrCat(pack->getName(), "::", ps->VpiName()));
      } else {
        ((type_parameter*)ps)
            ->VpiFullName(StrCat(pack->getName(), "::", ps->VpiName()));
      }
    }
  }

  // Param_assigns

  if (pack->getParam_assigns()) {
    p->Param_assigns(pack->getParam_assigns());
    for (auto ps : *p->Param_assigns()) {
      ps->VpiParent(p);
      reInstanceTypespec(s, ps, p);
    }
  }

  if (p->Typespecs()) {
    for (auto t : *p->Typespecs()) {
      reInstanceTypespec(s, t, p);
    }
  }

  if (p->Variables()) {
    for (auto v : *p->Variables()) {
      reInstanceTypespec(s, v, p);
    }
  }

  // Function and tasks
  if (pack->getTask_funcs()) {
    p->Task_funcs(s.MakeTask_funcVec());
    for (auto tf : *pack->getTask_funcs()) {
      const std::string_view funcName = tf->VpiName();
      if (funcName.find("::") != std::string::npos) {
        std::vector<std::string_view> res;
        StringUtils::tokenizeMulti(funcName, "::", res);
        const std::string_view className = res[0];
        const std::string_view funcName = res[1];
        bool foundParentClass = false;
        for (auto cl : *dest_classes) {
          if (cl->VpiName() == className) {
            tf->VpiParent(cl, true);
            tf->Instance(p);
            if (cl->Task_funcs() == nullptr) {
              cl->Task_funcs(s.MakeTask_funcVec());
            }
            cl->Task_funcs()->push_back(tf);
            foundParentClass = true;
            break;
          }
        }
        if (foundParentClass) {
          tf->VpiName(funcName);
          ((task_func*)tf)
              ->VpiFullName(StrCat(pack->getName(), "::", className,
                                   "::", tf->VpiName()));
        } else {
          tf->VpiParent(p);
          tf->Instance(p);
          p->Task_funcs()->push_back(tf);
          ((task_func*)tf)
              ->VpiFullName(StrCat(pack->getName(), "::", tf->VpiName()));
        }
      } else {
        tf->VpiParent(p);
        tf->Instance(p);
        p->Task_funcs()->push_back(tf);
        ((task_func*)tf)
            ->VpiFullName(StrCat(pack->getName(), "::", tf->VpiName()));
      }
    }
  }
  // Variables
  Netlist* netlist = pack->getNetlist();
  if (netlist) {
    p->Variables(netlist->variables());
    if (netlist->variables()) {
      for (auto obj : *netlist->variables()) {
        obj->VpiParent(p);
        ((variables*)obj)
            ->VpiFullName(StrCat(pack->getName(), "::", obj->VpiName()));
      }
    }
  }
  // Nets
  SignalBaseClassMap signalBaseMap;
  SignalMap portMap;
  SignalMap netMap;
  std::vector<Signal*> orig_nets = pack->getSignals();
  VectorOfnet* dest_nets = s.MakeNetVec();
  writeNets(pack, orig_nets, p, dest_nets, s, signalBaseMap, netMap, portMap,
            nullptr);
  p->Nets(dest_nets);
}

void UhdmWriter::writeModule(ModuleDefinition* mod, module_inst* m,
                             Serializer& s, ModuleMap& moduleMap,
                             ModPortMap& modPortMap, ModuleInstance* instance) {
  const ScopedScope scopedScope(m);
  SignalBaseClassMap signalBaseMap;
  SignalMap portMap;
  SignalMap netMap;

  m->VpiEndLabel(mod->getEndLabel());

  // Let decls
  if (!mod->getLetStmts().empty()) {
    VectorOflet_decl* decls = s.MakeLet_declVec();
    m->Let_decls(decls);
    for (auto& stmt : mod->getLetStmts()) {
      const_cast<let_decl*>(stmt.second->Decl())->VpiParent(m);
    }
  }
  // Gen stmts
  if (mod->getGenStmts()) {
    m->Gen_stmts(mod->getGenStmts());
    for (auto stmt : *mod->getGenStmts()) {
      stmt->VpiParent(m);
    }
  }
  if (!mod->getPropertyDecls().empty()) {
    VectorOfproperty_decl* decls = s.MakeProperty_declVec();
    m->Property_decls(decls);
    for (auto decl : mod->getPropertyDecls()) {
      decl->VpiParent(m);
    }
  }
  if (!mod->getSequenceDecls().empty()) {
    VectorOfsequence_decl* decls = s.MakeSequence_declVec();
    m->Sequence_decls(decls);
    for (auto decl : mod->getSequenceDecls()) {
      decl->VpiParent(m);
    }
  }

  // Ports
  std::vector<Signal*>& orig_ports = mod->getPorts();
  VectorOfport* dest_ports = s.MakePortVec();
  VectorOfnet* dest_nets = s.MakeNetVec();
  writePorts(orig_ports, m, dest_ports, dest_nets, s, modPortMap, signalBaseMap,
             portMap, instance, mod);
  m->Ports(dest_ports);
  // Nets
  mapLowConns(orig_ports, s, signalBaseMap);
  // Classes
  ClassNameClassDefinitionMultiMap& orig_classes = mod->getClassDefinitions();
  VectorOfclass_defn* dest_classes = s.MakeClass_defnVec();
  writeClasses(orig_classes, dest_classes, s, m);
  m->Class_defns(dest_classes);
  // Variables
  // DesignComponent::VariableMap& orig_vars = mod->getVariables();
  // VectorOfvariables* dest_vars = s.MakeVariablesVec();
  // writeVariables(orig_vars, m, dest_vars, s);
  // m->Variables(dest_vars);

  // Cont assigns
  m->Cont_assigns(mod->getContAssigns());
  if (m->Cont_assigns()) {
    for (auto ps : *m->Cont_assigns()) {
      ps->VpiParent(m);
    }
  }

  // Function and tasks
  if ((m_helper.getElaborate() == Elaborate::Yes) && m->Task_funcs()) {
    for (auto tf : *m->Task_funcs()) {
      if (tf->Instance() == nullptr) tf->Instance(m);
    }
  }

  // ClockingBlocks
  for (const auto& ctupple : mod->getClockingBlockMap()) {
    const ClockingBlock& cblock = ctupple.second;
    cblock.getActual()->VpiParent(m);
    switch (cblock.getType()) {
      case ClockingBlock::Type::Default: {
        m->Default_clocking(cblock.getActual());
        break;
      }
      case ClockingBlock::Type::Global: {
        m->Global_clocking(cblock.getActual());
        break;
      }
      case ClockingBlock::Type::Regular: {
        VectorOfclocking_block* cblocks = m->Clocking_blocks();
        if (cblocks == nullptr) {
          m->Clocking_blocks(s.MakeClocking_blockVec());
          cblocks = m->Clocking_blocks();
        }
        cblocks->push_back(cblock.getActual());
        break;
      }
    }
  }

  // Assertions
  if (mod->getAssertions()) {
    m->Assertions(mod->getAssertions());
    for (auto ps : *m->Assertions()) {
      ps->VpiParent(m);
    }
  }
  // Module Instantiation
  if (std::vector<UHDM::ref_module*>* subModules = mod->getRefModules()) {
    m->Ref_modules(subModules);
    for (auto subModArr : *subModules) {
      subModArr->VpiParent(m);
    }
  }
  if (VectorOfmodule_array* subModuleArrays = mod->getModuleArrays()) {
    m->Module_arrays(subModuleArrays);
    for (auto subModArr : *subModuleArrays) {
      subModArr->VpiParent(m);
    }
  }
  if (UHDM::VectorOfprimitive* subModules = mod->getPrimitives()) {
    m->Primitives(subModules);
    for (auto subModArr : *subModules) {
      subModArr->VpiParent(m);
    }
  }
  if (UHDM::VectorOfprimitive_array* subModules = mod->getPrimitiveArrays()) {
    m->Primitive_arrays(subModules);
    for (auto subModArr : *subModules) {
      subModArr->VpiParent(m);
    }
  }
  // Interface instantiation
  const std::vector<Signal*>& signals = mod->getSignals();
  if (!signals.empty()) {
    VectorOfinterface_array* subInterfaceArrays = s.MakeInterface_arrayVec();
    m->Interface_arrays(subInterfaceArrays);
    for (Signal* sig : signals) {
      NodeId unpackedDimension = sig->getUnpackedDimension();
      if (unpackedDimension && sig->getInterfaceDef()) {
        int32_t unpackedSize = 0;
        const FileContent* fC = sig->getFileContent();
        if (std::vector<UHDM::range*>* unpackedDimensions =
                m_helper.compileRanges(mod, fC, unpackedDimension,
                                       m_compileDesign, Reduce::No, nullptr,
                                       instance, unpackedSize, false)) {
          NodeId id = sig->getNodeId();
          const std::string typeName = sig->getInterfaceTypeName();
          interface_array* smarray = s.MakeInterface_array();
          smarray->Ranges(unpackedDimensions);
          for (auto d : *unpackedDimensions) d->VpiParent(smarray);
          if (fC->Type(sig->getNameId()) == VObjectType::slStringConst) {
            smarray->VpiName(sig->getName());
          }
          smarray->VpiFullName(typeName);
          smarray->VpiParent(m);
          fC->populateCoreMembers(id, id, smarray);

          NodeId typespecStart = sig->getInterfaceTypeNameId();
          NodeId typespecEnd = typespecStart;
          while (fC->Sibling(typespecEnd)) {
            typespecEnd = fC->Sibling(typespecEnd);
          }
          interface_typespec* tps = s.MakeInterface_typespec();
          if (smarray->Elem_typespec() == nullptr) {
            ref_typespec* tps_rt = s.MakeRef_typespec();
            tps_rt->VpiParent(smarray);
            smarray->Elem_typespec(tps_rt);
          }
          smarray->Elem_typespec()->Actual_typespec(tps);
          tps->VpiName(typeName);
          fC->populateCoreMembers(typespecStart, typespecEnd, tps);

          subInterfaceArrays->push_back(smarray);
        }
      }
    }
  }
}

void UhdmWriter::writeInterface(ModuleDefinition* mod, interface_inst* m,
                                Serializer& s, ModPortMap& modPortMap,
                                ModuleInstance* instance) {
  const ScopedScope scopedScope(m);

  SignalBaseClassMap signalBaseMap;
  SignalMap portMap;
  SignalMap netMap;

  m->VpiEndLabel(mod->getEndLabel());

  // Let decls
  if (!mod->getLetStmts().empty()) {
    VectorOflet_decl* decls = s.MakeLet_declVec();
    m->Let_decls(decls);
    for (auto stmt : mod->getLetStmts()) {
      decls->push_back((let_decl*)stmt.second->Decl());
    }
  }
  // Gen stmts
  if (mod->getGenStmts()) {
    m->Gen_stmts(mod->getGenStmts());
    for (auto stmt : *mod->getGenStmts()) {
      stmt->VpiParent(m);
    }
  }
  if (!mod->getPropertyDecls().empty()) {
    VectorOfproperty_decl* decls = s.MakeProperty_declVec();
    m->Property_decls(decls);
    for (auto decl : mod->getPropertyDecls()) {
      decl->VpiParent(m);
      decls->push_back(decl);
    }
  }
  if (!mod->getSequenceDecls().empty()) {
    VectorOfsequence_decl* decls = s.MakeSequence_declVec();
    m->Sequence_decls(decls);
    for (auto decl : mod->getSequenceDecls()) {
      decl->VpiParent(m);
      decls->push_back(decl);
    }
  }

  // Typepecs
  VectorOftypespec* typespecs = m->Typespecs();
  if (typespecs == nullptr) {
    typespecs = s.MakeTypespecVec();
    m->Typespecs(typespecs);
  }
  writeDataTypes(mod->getDataTypeMap(), m, typespecs, s, true);
  // Ports
  std::vector<Signal*>& orig_ports = mod->getPorts();
  VectorOfport* dest_ports = s.MakePortVec();
  VectorOfnet* dest_nets = s.MakeNetVec();
  writePorts(orig_ports, m, dest_ports, dest_nets, s, modPortMap, signalBaseMap,
             portMap, instance, mod);
  m->Ports(dest_ports);
  std::vector<Signal*> orig_nets = mod->getSignals();
  writeNets(mod, orig_nets, m, dest_nets, s, signalBaseMap, netMap, portMap,
            instance);
  m->Nets(dest_nets);
  // Modports
  ModuleDefinition::ModPortSignalMap& orig_modports =
      mod->getModPortSignalMap();
  VectorOfmodport* dest_modports = s.MakeModportVec();
  for (auto& orig_modport : orig_modports) {
    modport* dest_modport = s.MakeModport();
    // dest_modport->Interface(m); // Loop in elaboration!
    dest_modport->VpiParent(m);
    const FileContent* orig_fC = orig_modport.second.getFileContent();
    const NodeId orig_nodeId = orig_modport.second.getNodeId();
    orig_fC->populateCoreMembers(orig_nodeId, orig_nodeId, dest_modport);
    modPortMap.emplace(&orig_modport.second, dest_modport);
    dest_modport->VpiName(orig_modport.first);
    VectorOfio_decl* ios = s.MakeIo_declVec();
    for (auto& sig : orig_modport.second.getPorts()) {
      const FileContent* fC = sig.getFileContent();
      io_decl* io = s.MakeIo_decl();
      io->VpiName(sig.getName());
      fC->populateCoreMembers(sig.getNameId(), sig.getNameId(), io);
      if (NodeId Expression = fC->Sibling(sig.getNodeId())) {
        m_helper.checkForLoops(true);
        if (any* exp =
                m_helper.compileExpression(mod, fC, Expression, m_compileDesign,
                                           Reduce::Yes, io, instance, true)) {
          io->Expr(exp);
        }
        m_helper.checkForLoops(false);
      }
      uint32_t direction = UhdmWriter::getVpiDirection(sig.getDirection());
      io->VpiDirection(direction);
      io->VpiParent(dest_modport);
      ios->push_back(io);
    }
    dest_modport->Io_decls(ios);
    dest_modports->push_back(dest_modport);
  }
  m->Modports(dest_modports);

  // Cont assigns
  if (mod->getContAssigns()) {
    m->Cont_assigns(mod->getContAssigns());
    for (auto ps : *m->Cont_assigns()) {
      ps->VpiParent(m);
    }
  }

  // Assertions
  if (mod->getAssertions()) {
    m->Assertions(mod->getAssertions());
    for (auto ps : *m->Assertions()) {
      ps->VpiParent(m);
    }
  }

  // Processes
  if (mod->getProcesses()) {
    m->Process(mod->getProcesses());
    for (auto ps : *m->Process()) {
      ps->VpiParent(m);
    }
  }

  // Function and tasks
  if ((m_helper.getElaborate() == Elaborate::Yes) && m->Task_funcs()) {
    for (auto tf : *m->Task_funcs()) {
      if (tf->Instance() == nullptr) tf->Instance(m);
    }
  }

  // ClockingBlocks
  for (const auto& ctupple : mod->getClockingBlockMap()) {
    const ClockingBlock& cblock = ctupple.second;
    cblock.getActual()->VpiParent(m);
    switch (cblock.getType()) {
      case ClockingBlock::Type::Default: {
        m->Default_clocking(cblock.getActual());
        break;
      }
      case ClockingBlock::Type::Global: {
        m->Global_clocking(cblock.getActual());
        break;
      }
      case ClockingBlock::Type::Regular: {
        VectorOfclocking_block* cblocks = m->Clocking_blocks();
        if (cblocks == nullptr) {
          m->Clocking_blocks(s.MakeClocking_blockVec());
          cblocks = m->Clocking_blocks();
        }
        cblocks->push_back(cblock.getActual());
        break;
      }
    }
  }
}

void UhdmWriter::writeProgram(Program* mod, program* m, Serializer& s,
                              ModPortMap& modPortMap,
                              ModuleInstance* instance) {
  const ScopedScope scopedScope(m);

  SignalBaseClassMap signalBaseMap;
  SignalMap portMap;
  SignalMap netMap;

  m->VpiEndLabel(mod->getEndLabel());

  // Typepecs
  VectorOftypespec* typespecs = m->Typespecs();
  if (typespecs == nullptr) {
    typespecs = s.MakeTypespecVec();
    m->Typespecs(typespecs);
  }
  writeDataTypes(mod->getDataTypeMap(), m, typespecs, s, true);
  // Ports
  std::vector<Signal*>& orig_ports = mod->getPorts();
  VectorOfport* dest_ports = s.MakePortVec();
  VectorOfnet* dest_nets = s.MakeNetVec();
  writePorts(orig_ports, m, dest_ports, dest_nets, s, modPortMap, signalBaseMap,
             portMap, instance, mod);
  m->Ports(dest_ports);
  // Nets
  std::vector<Signal*>& orig_nets = mod->getSignals();
  writeNets(mod, orig_nets, m, dest_nets, s, signalBaseMap, netMap, portMap,
            instance);
  m->Nets(dest_nets);
  mapLowConns(orig_ports, s, signalBaseMap);

  // Assertions
  if (mod->getAssertions()) {
    m->Assertions(mod->getAssertions());
    for (auto ps : *m->Assertions()) {
      ps->VpiParent(m);
    }
  }

  // Classes
  ClassNameClassDefinitionMultiMap& orig_classes = mod->getClassDefinitions();
  VectorOfclass_defn* dest_classes = s.MakeClass_defnVec();
  writeClasses(orig_classes, dest_classes, s, m);
  m->Class_defns(dest_classes);
  // Variables
  const DesignComponent::VariableMap& orig_vars = mod->getVariables();
  VectorOfvariables* dest_vars = s.MakeVariablesVec();
  writeVariables(orig_vars, m, dest_vars, s);
  m->Variables(dest_vars);

  // Cont assigns
  m->Cont_assigns(mod->getContAssigns());
  if (m->Cont_assigns()) {
    for (auto ps : *m->Cont_assigns()) {
      ps->VpiParent(m);
    }
  }
  // Processes
  m->Process(mod->getProcesses());
  if (m->Process()) {
    for (auto ps : *m->Process()) {
      ps->VpiParent(m);
    }
  }
  m->Task_funcs(mod->getTask_funcs());
  if (m->Task_funcs()) {
    for (auto tf : *m->Task_funcs()) {
      tf->VpiParent(m);
    }
  }

  // ClockingBlocks
  for (const auto& ctupple : mod->getClockingBlockMap()) {
    const ClockingBlock& cblock = ctupple.second;
    cblock.getActual()->VpiParent(m);
    switch (cblock.getType()) {
      case ClockingBlock::Type::Default: {
        m->Default_clocking(cblock.getActual());
        break;
      }
      case ClockingBlock::Type::Regular: {
        VectorOfclocking_block* cblocks = m->Clocking_blocks();
        if (cblocks == nullptr) {
          m->Clocking_blocks(s.MakeClocking_blockVec());
          cblocks = m->Clocking_blocks();
        }
        cblocks->push_back(cblock.getActual());
        break;
      }
      default:
        break;
    }
  }
}

bool UhdmWriter::writeElabProgram(Serializer& s, ModuleInstance* instance,
                                  program* m, ModPortMap& modPortMap) {
  Netlist* netlist = instance->getNetlist();
  DesignComponent* mod = instance->getDefinition();
  if (mod) {
    // Let decls
    if (!mod->getLetStmts().empty()) {
      VectorOflet_decl* decls = s.MakeLet_declVec();
      m->Let_decls(decls);
      for (auto stmt : mod->getLetStmts()) {
        decls->push_back((let_decl*)stmt.second->Decl());
      }
    }
    if (!mod->getPropertyDecls().empty()) {
      VectorOfproperty_decl* decls = s.MakeProperty_declVec();
      m->Property_decls(decls);
      for (auto decl : mod->getPropertyDecls()) {
        decl->VpiParent(m);
        decls->push_back(decl);
      }
    }
    if (!mod->getSequenceDecls().empty()) {
      VectorOfsequence_decl* decls = s.MakeSequence_declVec();
      m->Sequence_decls(decls);
      for (auto decl : mod->getSequenceDecls()) {
        decl->VpiParent(m);
        decls->push_back(decl);
      }
    }
    // Typepecs
    VectorOftypespec* typespecs = m->Typespecs();
    if (typespecs == nullptr) {
      typespecs = s.MakeTypespecVec();
      m->Typespecs(typespecs);
    }
    writeDataTypes(mod->getDataTypeMap(), m, typespecs, s, false);
    // Assertions
    if (mod->getAssertions()) {
      m->Assertions(mod->getAssertions());
      for (auto ps : *m->Assertions()) {
        ps->VpiParent(m);
      }
    }
  }
  if (netlist) {
    m->Ports(netlist->ports());
    if (netlist->ports()) {
      typedef std::map<const UHDM::modport*, ModPort*> PortModPortMap;
      PortModPortMap portModPortMap;
      for (auto& entry : netlist->getModPortMap()) {
        portModPortMap.emplace(entry.second.second, entry.second.first);
      }

      for (auto obj : *netlist->ports()) {
        obj->VpiParent(m);

        if (auto lc = obj->Low_conn()) {
          if (const ref_obj* ro = any_cast<const ref_obj*>(lc)) {
            if (const any* ag = ro->Actual_group()) {
              if (ag->UhdmType() == UHDM::uhdmmodport) {
                PortModPortMap::const_iterator it1 =
                    portModPortMap.find((const UHDM::modport*)ag);
                if (it1 != portModPortMap.end()) {
                  ModPortMap::const_iterator it2 = modPortMap.find(it1->second);
                  if (it2 != modPortMap.end()) {
                    const_cast<ref_obj*>(ro)->Actual_group(it2->second);
                  }
                }
              }
            }
          }
        }
      }
    }
    m->Nets(netlist->nets());
    if (netlist->nets()) {
      for (auto obj : *netlist->nets()) {
        obj->VpiParent(m);
      }
    }
    m->Gen_scope_arrays(netlist->gen_scopes());
    m->Variables(netlist->variables());
    if (netlist->variables()) {
      for (auto obj : *netlist->variables()) {
        obj->VpiParent(m);
      }
    }
    m->Array_vars(netlist->array_vars());
    if (netlist->array_vars()) {
      for (auto obj : *netlist->array_vars()) {
        obj->VpiParent(m);
      }
    }
    m->Named_events(netlist->named_events());
    if (netlist->named_events()) {
      for (auto obj : *netlist->named_events()) {
        obj->VpiParent(m);
      }
    }
    m->Array_nets(netlist->array_nets());
    if (netlist->array_nets()) {
      for (auto obj : *netlist->array_nets()) {
        obj->VpiParent(m);
      }
    }

    // Cont assigns
    m->Cont_assigns(mod->getContAssigns());
    if (m->Cont_assigns()) {
      for (auto ps : *m->Cont_assigns()) {
        ps->VpiParent(m);
      }
    }
    // Processes
    m->Process(mod->getProcesses());
    if (m->Process()) {
      for (auto ps : *m->Process()) {
        ps->VpiParent(m);
      }
    }
  }
  if (mod) {
    if (auto from = mod->getTask_funcs()) {
      UHDM::VectorOftask_func* target = m->Task_funcs();
      if (target == nullptr) {
        m->Task_funcs(s.MakeTask_funcVec());
        target = m->Task_funcs();
      }
      for (auto tf : *from) {
        target->push_back(tf);
        if (tf->VpiParent() == nullptr) tf->VpiParent(m);
        if (tf->Instance() == nullptr) tf->Instance(m);
      }
    }
  }

  if (mod) {
    // ClockingBlocks
    ModuleDefinition* def = (ModuleDefinition*)mod;
    for (const auto& ctupple : def->getClockingBlockMap()) {
      const ClockingBlock& cblock = ctupple.second;
      switch (cblock.getType()) {
        case ClockingBlock::Type::Default: {
          ElaboratorContext elaboratorContext(&s, false, true);
          clocking_block* cb = (clocking_block*)UHDM::clone_tree(
              cblock.getActual(), &elaboratorContext);
          cb->VpiParent(m);
          m->Default_clocking(cb);
          break;
        }
        case ClockingBlock::Type::Global: {
          // m->Global_clocking(cblock.getActual());
          break;
        }
        case ClockingBlock::Type::Regular: {
          ElaboratorContext elaboratorContext(&s, false, true);
          clocking_block* cb = (clocking_block*)UHDM::clone_tree(
              cblock.getActual(), &elaboratorContext);
          cb->VpiParent(m);
          VectorOfclocking_block* cblocks = m->Clocking_blocks();
          if (cblocks == nullptr) {
            m->Clocking_blocks(s.MakeClocking_blockVec());
            cblocks = m->Clocking_blocks();
          }
          cblocks->push_back(cb);
          break;
        }
      }
    }
  }

  return true;
}

class DetectUnsizedConstant final : public VpiListener {
 public:
  DetectUnsizedConstant() {}
  bool unsizedDetected() { return unsized_; }

 private:
  void leaveConstant(const constant* object, vpiHandle handle) final {
    if (object->VpiSize() == -1) unsized_ = true;
  }
  bool unsized_ = false;
};

void UhdmWriter::writeCont_assign(Netlist* netlist, Serializer& s,
                                  DesignComponent* mod, any* m,
                                  std::vector<cont_assign*>* assigns) {
  FileSystem* const fileSystem = FileSystem::getInstance();
  if (netlist->cont_assigns()) {
    for (auto assign : *netlist->cont_assigns()) {
      const expr* lhs = assign->Lhs();
      const expr* rhs = assign->Rhs();
      const expr* delay = assign->Delay();
      const typespec* tps = nullptr;
      if (const ref_typespec* rt = lhs->Typespec()) {
        tps = rt->Actual_typespec();
      }
      bool simplified = false;
      bool cloned = false;
      if (delay && delay->UhdmType() == uhdmref_obj) {
        UHDM::any* var = m_helper.bindParameter(
            mod, netlist->getParent(), delay->VpiName(), m_compileDesign, true);
        ElaboratorContext elaboratorContext(&s, false, true);
        assign = (cont_assign*)UHDM::clone_tree(assign, &elaboratorContext);
        lhs = assign->Lhs();
        rhs = assign->Rhs();
        if (const ref_typespec* rt = lhs->Typespec()) {
          tps = rt->Actual_typespec();
        }
        delay = assign->Delay();
        ref_obj* ref = (ref_obj*)delay;
        ref->Actual_group(var);
        cloned = true;
      }

      if (lhs->UhdmType() == uhdmref_obj) {
        UHDM::any* var =
            m_helper.bindVariable(mod, m, lhs->VpiName(), m_compileDesign);
        if (var) {
          if (rhs->UhdmType() == uhdmoperation) {
            if (cloned == false) {
              ElaboratorContext elaboratorContext(&s, false, true);
              const UHDM::any* pp = assign->VpiParent();
              assign =
                  (cont_assign*)UHDM::clone_tree(assign, &elaboratorContext);
              if (pp != nullptr) assign->VpiParent(const_cast<UHDM::any*>(pp));
              lhs = assign->Lhs();
              rhs = assign->Rhs();
              m_helper.checkForLoops(true);
              bool invalidValue = false;
              any* rhstmp = m_helper.reduceExpr(
                  (expr*)rhs, invalidValue, mod, m_compileDesign,
                  netlist->getParent(),
                  fileSystem->toPathId(
                      rhs->VpiFile(),
                      m_compileDesign->getCompiler()->getSymbolTable()),
                  rhs->VpiLineNo(), assign, true);
              m_helper.checkForLoops(false);
              if (const ref_typespec* rt = lhs->Typespec()) {
                tps = rt->Actual_typespec();
              }
              if (expr* exp = any_cast<expr*>(var)) {
                if (const ref_typespec* rt = exp->Typespec()) {
                  if (const typespec* temp = rt->Actual_typespec()) {
                    tps = temp;
                  }
                }
              }
              if (!invalidValue && rhstmp) {
                if (rhstmp->UhdmType() == uhdmconstant)
                  rhstmp = m_helper.adjustSize(tps, mod, m_compileDesign,
                                               netlist->getParent(),
                                               (constant*)rhstmp, true);
                assign->Rhs((expr*)rhstmp);
              }
              rhs = assign->Rhs();
              delay = assign->Delay();
            }
            ref_obj* ref = (ref_obj*)lhs;
            ref->Actual_group(var);
            cloned = true;
          }

          if (expr* exp = any_cast<expr*>(var)) {
            if (const ref_typespec* rt = exp->Typespec()) {
              if (const typespec* temp = rt->Actual_typespec()) {
                tps = temp;
              }
            }
          }
        }
      }
      if (tps) {
        UHDM::ExprEval eval(true);
        expr* tmp = eval.flattenPatternAssignments(s, tps, (expr*)rhs);
        if (tmp->UhdmType() == uhdmoperation) {
          if (cloned == false) {
            ElaboratorContext elaboratorContext(&s, false, true);
            const UHDM::any* pp = assign->VpiParent();
            assign = (cont_assign*)UHDM::clone_tree(assign, &elaboratorContext);
            if (pp != nullptr) assign->VpiParent(const_cast<UHDM::any*>(pp));
            assign->VpiParent(m);
            lhs = assign->Lhs();
            rhs = assign->Rhs();
            delay = assign->Delay();
            if (const ref_typespec* rt = lhs->Typespec()) {
              tps = rt->Actual_typespec();
            }
            cloned = true;
          }
          ((operation*)rhs)->Operands(((operation*)tmp)->Operands());
          for (auto o : *((operation*)tmp)->Operands()) {
            o->VpiParent(const_cast<expr*>(rhs));
          }
          operation* op = (operation*)rhs;
          int32_t opType = op->VpiOpType();
          if (opType == vpiAssignmentPatternOp || opType == vpiConditionOp) {
            if (m_helper.substituteAssignedValue(rhs, m_compileDesign)) {
              rhs = m_helper.expandPatternAssignment(tps, (UHDM::expr*)rhs, mod,
                                                     m_compileDesign,
                                                     netlist->getParent());
            }
          }
          rhs = (UHDM::expr*)m_helper.defaultPatternAssignment(
              tps, (UHDM::expr*)rhs, mod, m_compileDesign,
              netlist->getParent());

          assign->Rhs((UHDM::expr*)rhs);
          m_helper.reorderAssignmentPattern(mod, lhs, (UHDM::expr*)rhs,
                                            m_compileDesign,
                                            netlist->getParent(), 0);
          simplified = true;
        }
      } else if (rhs->UhdmType() == uhdmoperation) {
        operation* op = (operation*)rhs;
        if (const ref_typespec* ro1 = op->Typespec()) {
          if (const typespec* tps = ro1->Actual_typespec()) {
            UHDM::ExprEval eval(true);
            expr* tmp = eval.flattenPatternAssignments(s, tps, (expr*)rhs);
            if (tmp->UhdmType() == uhdmoperation) {
              if (cloned == false) {
                ElaboratorContext elaboratorContext(&s, false, true);
                assign =
                    (cont_assign*)UHDM::clone_tree(assign, &elaboratorContext);
                assign->VpiParent(m);
                lhs = assign->Lhs();
                rhs = assign->Rhs();
                delay = assign->Delay();
                if (const ref_typespec* ro2 = lhs->Typespec()) {
                  tps = ro2->Actual_typespec();
                }
                cloned = true;
              }
              ((operation*)rhs)->Operands(((operation*)tmp)->Operands());
              simplified = true;
            }
          }
        }
      }
      if (simplified == false) {
        bool invalidValue = false;
        if ((rhs->UhdmType() == uhdmsys_func_call) &&
            ((expr*)rhs)->Typespec() == nullptr) {
          if (((expr*)rhs)->Typespec() == nullptr) {
            ref_typespec* crt = s.MakeRef_typespec();
            crt->VpiParent((expr*)rhs);
            ((expr*)rhs)->Typespec(crt);
          }
          ((expr*)rhs)->Typespec()->Actual_typespec(const_cast<typespec*>(tps));
        }
        m_helper.checkForLoops(true);
        any* res = m_helper.reduceExpr(
            (expr*)rhs, invalidValue, mod, m_compileDesign,
            netlist->getParent(),
            fileSystem->toPathId(
                rhs->VpiFile(),
                m_compileDesign->getCompiler()->getSymbolTable()),
            rhs->VpiLineNo(), assign, true);
        m_helper.checkForLoops(false);
        if (!invalidValue && res && (res->UhdmType() == uhdmconstant)) {
          if (cloned == false) {
            ElaboratorContext elaboratorContext(&s, false, true);
            assign = (cont_assign*)UHDM::clone_tree(assign, &elaboratorContext);
            assign->VpiParent(m);
            lhs = assign->Lhs();
            cloned = true;
            res =
                m_helper.adjustSize(tps, mod, m_compileDesign,
                                    netlist->getParent(), (constant*)res, true);
            res->VpiParent(assign);
          }
          assign->Rhs((constant*)res);
        }
      }
      if (simplified == false && cloned == false) {
        DetectUnsizedConstant detector;
        vpiHandle h_rhs = NewVpiHandle(rhs);
        detector.listenAny(h_rhs);
        vpi_free_object(h_rhs);
        if (detector.unsizedDetected()) {
          ElaboratorContext elaboratorContext(&s, false, true);
          assign = (cont_assign*)UHDM::clone_tree(assign, &elaboratorContext);
          assign->VpiParent(m);
          cloned = true;
        }
      }
      if (tps != nullptr) const_cast<typespec*>(tps)->VpiParent(nullptr);
      if (cloned || (assign->VpiParent() == nullptr)) assign->VpiParent(m);
      assigns->push_back(assign);
    }
  }
}

bool UhdmWriter::writeElabGenScope(Serializer& s, ModuleInstance* instance,
                                   gen_scope* m, ExprBuilder& exprBuilder) {
  FileSystem* const fileSystem = FileSystem::getInstance();
  Netlist* netlist = instance->getNetlist();
  ModuleDefinition* mod =
      valuedcomponenti_cast<ModuleDefinition*>(instance->getDefinition());
  if (mod) {
    // Let decls
    if (!mod->getLetStmts().empty()) {
      VectorOflet_decl* decls = s.MakeLet_declVec();
      m->Let_decls(decls);
      for (auto stmt : mod->getLetStmts()) {
        decls->push_back((let_decl*)stmt.second->Decl());
      }
    }
    if (!mod->getPropertyDecls().empty()) {
      VectorOfproperty_decl* decls = s.MakeProperty_declVec();
      m->Property_decls(decls);
      for (auto decl : mod->getPropertyDecls()) {
        decl->VpiParent(m);
        decls->push_back(decl);
      }
    }
    if (!mod->getSequenceDecls().empty()) {
      VectorOfsequence_decl* decls = s.MakeSequence_declVec();
      m->Sequence_decls(decls);
      for (auto decl : mod->getSequenceDecls()) {
        decl->VpiParent(m);
        decls->push_back(decl);
      }
    }
    // Typepecs
    VectorOftypespec* typespecs = m->Typespecs();
    if (typespecs == nullptr) {
      typespecs = s.MakeTypespecVec();
      m->Typespecs(typespecs);
    }
    writeDataTypes(mod->getDataTypeMap(), m, typespecs, s, true);
    // System elab tasks
    m->Elab_tasks((std::vector<UHDM::tf_call*>*)&mod->getElabSysCalls());
    if (m->Elab_tasks()) {
      for (auto et : *m->Elab_tasks()) {
        et->VpiParent(m);
      }
    }
    // Assertions
    if (mod->getAssertions()) {
      m->Assertions(mod->getAssertions());
      for (auto ps : *m->Assertions()) {
        ps->VpiParent(m);
      }
    }
  }
  if (netlist) {
    m->Nets(netlist->nets());
    if (netlist->nets()) {
      for (auto obj : *netlist->nets()) {
        obj->VpiParent(m);
      }
    }

    if (netlist->cont_assigns()) {
      std::vector<cont_assign*>* assigns = m->Cont_assigns();
      if (assigns == nullptr) {
        m->Cont_assigns(s.MakeCont_assignVec());
        assigns = m->Cont_assigns();
      }
      writeCont_assign(netlist, s, mod, m, assigns);
      for (auto obj : *assigns) {
        obj->VpiParent(m);
      }
    }

    // Processes
    m->Process(netlist->process_stmts());
    if (m->Process()) {
      for (auto ps : *m->Process()) {
        ps->VpiParent(m);
      }
    }

    std::vector<gen_scope_array*>* gen_scope_arrays = netlist->gen_scopes();
    if (gen_scope_arrays) {
      writeElabParameters(s, instance, m, exprBuilder);

      // Loop indexes
      for (auto& param : instance->getMappedValues()) {
        const std::string_view name = param.first;
        Value* val = param.second.first;
        VectorOfany* params = m->Parameters();
        if (params == nullptr) {
          params = s.MakeAnyVec();
        }
        m->Parameters(params);
        bool found = false;
        for (auto p : *params) {
          if (p->VpiName() == name) {
            found = true;
            break;
          }
        }
        if (!found) {
          parameter* p = s.MakeParameter();
          p->VpiName(name);
          if (val && val->isValid()) p->VpiValue(val->uhdmValue());
          p->VpiFile(fileSystem->toPath(instance->getFileId()));
          p->VpiLineNo(param.second.second);
          p->VpiParent(m);
          p->VpiLocalParam(true);
          int_typespec* ts = s.MakeInt_typespec();
          ref_typespec* rt = s.MakeRef_typespec();
          rt->VpiParent(p);
          p->Typespec(rt);
          rt->Actual_typespec(ts);
          params->push_back(p);
        }
      }
    }

    m->Variables(netlist->variables());
    if (netlist->variables()) {
      for (auto obj : *netlist->variables()) {
        obj->VpiParent(m);
      }
    }
    m->Array_vars(netlist->array_vars());
    if (netlist->array_vars()) {
      for (auto obj : *netlist->array_vars()) {
        obj->VpiParent(m);
      }
    }
    m->Named_events(netlist->named_events());
    if (netlist->named_events()) {
      for (auto obj : *netlist->named_events()) {
        obj->VpiParent(m);
      }
    }
    m->Array_nets(netlist->array_nets());
    if (netlist->array_nets()) {
      for (auto obj : *netlist->array_nets()) {
        obj->VpiParent(m);
      }
    }
  }

  DesignComponent* def = instance->getDefinition();
  if (def->getTask_funcs()) {
    // Function and tasks
    UHDM::VectorOftask_func* target = m->Task_funcs();
    if (target == nullptr) {
      m->Task_funcs(s.MakeTask_funcVec());
      target = m->Task_funcs();
    }
    for (auto tf : *def->getTask_funcs()) {
      target->push_back(tf);
      if (tf->VpiParent() == nullptr) tf->VpiParent(m);
    }
  }

  // ClockingBlocks
  for (const auto& ctupple : mod->getClockingBlockMap()) {
    const ClockingBlock& cblock = ctupple.second;
    switch (cblock.getType()) {
      case ClockingBlock::Type::Default: {
        // No default clocking
        // m->Default_clocking(cblock.getActual());
        break;
      }
      case ClockingBlock::Type::Regular: {
        ElaboratorContext elaboratorContext(&s, false, true);
        clocking_block* cb = (clocking_block*)UHDM::clone_tree(
            cblock.getActual(), &elaboratorContext);
        cb->VpiParent(m);
        VectorOfclocking_block* cblocks = m->Clocking_blocks();
        if (cblocks == nullptr) {
          m->Clocking_blocks(s.MakeClocking_blockVec());
          cblocks = m->Clocking_blocks();
        }
        cblocks->push_back(cb);
        break;
      }
      default:
        break;
    }
  }

  return true;
}

UHDM::any* UhdmWriter::swapForSpecifiedVar(UHDM::Serializer& s,
                                           DesignComponent* mod, any* tmp,
                                           VectorOfvariables* lvariables,
                                           variables* lvariable,
                                           std::string_view name,
                                           const any* var, const any* parent) {
  FileSystem* const fileSystem = FileSystem::getInstance();
  if (tmp->VpiName() == name) {
    if (var->UhdmType() == uhdmref_var) {
      ref_var* ref = (ref_var*)var;
      const typespec* tp = nullptr;
      if (ref_typespec* rt = ref->Typespec()) tp = rt->Actual_typespec();
      if (tp && tp->UhdmType() == uhdmunsupported_typespec) {
        const typespec* indexTypespec = nullptr;
        if (lvariables) {
          for (auto var : *lvariables) {
            if (var->UhdmType() == uhdmhier_path) {
              PathId parentFileId = fileSystem->toPathId(
                  parent->VpiFile(),
                  m_compileDesign->getCompiler()->getSymbolTable());
              bool invalidValue = false;
              indexTypespec = (typespec*)m_helper.decodeHierPath(
                  (hier_path*)var, invalidValue, mod, m_compileDesign,
                  Reduce::Yes, nullptr, parentFileId, parent->VpiLineNo(),
                  (any*)parent, true /*mute for now*/, true);
            }
          }
        } else if (const variables* var = lvariable) {
          if (var->UhdmType() == uhdmhier_path) {
            PathId parentFileId = fileSystem->toPathId(
                parent->VpiFile(),
                m_compileDesign->getCompiler()->getSymbolTable());
            bool invalidValue = false;
            indexTypespec = (typespec*)m_helper.decodeHierPath(
                (hier_path*)var, invalidValue, mod, m_compileDesign,
                Reduce::Yes, nullptr, parentFileId, parent->VpiLineNo(),
                (any*)parent, true /*mute for now*/, true);
          } else if (var->UhdmType() == uhdmref_var) {
            bool invalidValue = false;
            hier_path* path = s.MakeHier_path();
            VectorOfany* elems = s.MakeAnyVec();
            path->Path_elems(elems);
            ref_obj* ref = s.MakeRef_obj();
            elems->push_back(ref);
            ref->VpiName(var->VpiName());
            path->VpiFullName(var->VpiName());
            PathId parentFileId = fileSystem->toPathId(
                parent->VpiFile(),
                m_compileDesign->getCompiler()->getSymbolTable());
            indexTypespec = (typespec*)m_helper.decodeHierPath(
                path, invalidValue, mod, m_compileDesign, Reduce::Yes, nullptr,
                parentFileId, parent->VpiLineNo(), (any*)parent,
                true /*mute for now*/, true);
          }
        }
        variables* swapVar = nullptr;
        if (indexTypespec) {
          swapVar = m_helper.getSimpleVarFromTypespec(
              nullptr, InvalidNodeId, InvalidNodeId, (typespec*)indexTypespec,
              nullptr, m_compileDesign);
        } else {
          int_var* ivar = s.MakeInt_var();
          ref_typespec* rt = s.MakeRef_typespec();
          rt->Actual_typespec(s.MakeInt_typespec());
          rt->VpiParent(ivar);
          ivar->Typespec(rt);
          swapVar = ivar;
        }
        if (swapVar) {
          swapVar->VpiName(ref->VpiName());
          swapVar->VpiParent(const_cast<any*>(ref->VpiParent()));
          swapVar->VpiFile(ref->VpiFile());
          swapVar->VpiLineNo(ref->VpiLineNo());
          swapVar->VpiColumnNo(ref->VpiColumnNo());
          swapVar->VpiEndLineNo(ref->VpiEndLineNo());
          swapVar->VpiEndColumnNo(ref->VpiEndColumnNo());
          tmp = swapVar;
        }
      }
    }
  }
  return tmp;
}

void UhdmWriter::bind(UHDM::Serializer& s,
                      const std::vector<vpiHandle>& designs) {
  Compiler* const compiler = m_compileDesign->getCompiler();
  SymbolTable* const symbolTable = compiler->getSymbolTable();
  ErrorContainer* const errorContainer = compiler->getErrorContainer();
  CommandLineParser* commandLineParser = compiler->getCommandLineParser();
  if (ObjectBinder* const listener =
          new ObjectBinder(m_componentMap, s, symbolTable, errorContainer,
                           commandLineParser->muteStdout())) {
    for (auto h : designs) {
      const design* const d =
          static_cast<const design*>(((const uhdm_handle*)h)->object);
      listener->listenDesign(d);
    }
    while (listener->createDefaultNets());
    listener->reportErrors();
    delete listener;
  }
}

bool UhdmWriter::writeElabModule(Serializer& s, ModuleInstance* instance,
                                 module_inst* m, ExprBuilder& exprBuilder) {
  Netlist* netlist = instance->getNetlist();
  if (netlist == nullptr) return true;
  m->Ports(netlist->ports());
  DesignComponent* mod = instance->getDefinition();
  if (mod) {
    // Let decls
    if (!mod->getLetStmts().empty()) {
      VectorOflet_decl* decls = s.MakeLet_declVec();
      m->Let_decls(decls);
      for (auto stmt : mod->getLetStmts()) {
        decls->push_back((let_decl*)stmt.second->Decl());
      }
    }
    if (!mod->getPropertyDecls().empty()) {
      VectorOfproperty_decl* decls = s.MakeProperty_declVec();
      m->Property_decls(decls);
      for (auto decl : mod->getPropertyDecls()) {
        decl->VpiParent(m);
        decls->push_back(decl);
      }
    }
    // Typepecs
    VectorOftypespec* typespecs = m->Typespecs();
    if (typespecs == nullptr) {
      typespecs = s.MakeTypespecVec();
      m->Typespecs(typespecs);
    }
    writeDataTypes(mod->getDataTypeMap(), m, typespecs, s, false);
    // System elab tasks
    m->Elab_tasks((std::vector<UHDM::tf_call*>*)&mod->getElabSysCalls());
    if (m->Elab_tasks()) {
      for (auto et : *m->Elab_tasks()) {
        et->VpiParent(m);
      }
    }
    // Assertions
    if (mod->getAssertions()) {
      m->Assertions(mod->getAssertions());
      for (auto ps : *m->Assertions()) {
        ps->VpiParent(m);
      }
    }
  }

  writeElabParameters(s, instance, m, exprBuilder);
  if (netlist) {
    if (netlist->ports()) {
      for (auto obj : *netlist->ports()) {
        obj->VpiParent(m);
      }
    }
    m->Nets(netlist->nets());
    if (netlist->nets()) {
      for (auto obj : *netlist->nets()) {
        obj->VpiParent(m);
      }
    }
    m->Gen_scope_arrays(netlist->gen_scopes());
    if (netlist->gen_scopes()) {
      for (auto obj : *netlist->gen_scopes()) {
        obj->VpiParent(m);
      }
    }
    m->Variables(netlist->variables());
    if (netlist->variables()) {
      for (auto obj : *netlist->variables()) {
        obj->VpiParent(m);
      }
    }
    m->Array_vars(netlist->array_vars());
    if (netlist->array_vars()) {
      for (auto obj : *netlist->array_vars()) {
        obj->VpiParent(m);
      }
    }
    m->Named_events(netlist->named_events());
    if (netlist->named_events()) {
      for (auto obj : *netlist->named_events()) {
        obj->VpiParent(m);
      }
    }
    m->Array_nets(netlist->array_nets());
    if (netlist->array_nets()) {
      for (auto obj : *netlist->array_nets()) {
        obj->VpiParent(m);
      }
    }

    if (netlist->cont_assigns()) {
      std::vector<cont_assign*>* assigns = m->Cont_assigns();
      if (assigns == nullptr) {
        m->Cont_assigns(s.MakeCont_assignVec());
        assigns = m->Cont_assigns();
      }
      writeCont_assign(netlist, s, mod, m, assigns);
    }

    // Processes
    m->Process(netlist->process_stmts());
  }

  if (mod) {
    // ClockingBlocks
    ModuleDefinition* def = (ModuleDefinition*)mod;
    for (const auto& ctupple : def->getClockingBlockMap()) {
      const ClockingBlock& cblock = ctupple.second;
      ElaboratorContext elaboratorContext(&s, false, true);
      clocking_block* cb = (clocking_block*)UHDM::clone_tree(
          cblock.getActual(), &elaboratorContext);
      cb->VpiParent(m);
      switch (cblock.getType()) {
        case ClockingBlock::Type::Default: {
          m->Default_clocking(cb);
          break;
        }
        case ClockingBlock::Type::Global: {
          m->Global_clocking(cb);
          break;
        }
        case ClockingBlock::Type::Regular: {
          VectorOfclocking_block* cblocks = m->Clocking_blocks();
          if (cblocks == nullptr) {
            m->Clocking_blocks(s.MakeClocking_blockVec());
            cblocks = m->Clocking_blocks();
          }
          cblocks->push_back(cb);
          break;
        }
      }
    }
  }

  if (mod) {
    if (auto from = mod->getTask_funcs()) {
      UHDM::VectorOftask_func* target = m->Task_funcs();
      if (target == nullptr) {
        m->Task_funcs(s.MakeTask_funcVec());
        target = m->Task_funcs();
      }
      for (auto tf : *from) {
        target->push_back(tf);
        if (tf->VpiParent() == nullptr) tf->VpiParent(m);
        if (tf->Instance() == nullptr) tf->Instance(m);
      }
    }
  }
  return true;
}

bool UhdmWriter::writeElabInterface(Serializer& s, ModuleInstance* instance,
                                    interface_inst* m,
                                    ExprBuilder& exprBuilder) {
  Netlist* netlist = instance->getNetlist();
  DesignComponent* mod = instance->getDefinition();
  if (mod) {
    // Let decls
    if (!mod->getLetStmts().empty()) {
      VectorOflet_decl* decls = s.MakeLet_declVec();
      m->Let_decls(decls);
      for (auto stmt : mod->getLetStmts()) {
        decls->push_back((let_decl*)stmt.second->Decl());
      }
    }
    if (!mod->getPropertyDecls().empty()) {
      VectorOfproperty_decl* decls = s.MakeProperty_declVec();
      m->Property_decls(decls);
      for (auto decl : mod->getPropertyDecls()) {
        decl->VpiParent(m);
        decls->push_back(decl);
      }
    }
    // Typepecs
    VectorOftypespec* typespecs = m->Typespecs();
    if (typespecs == nullptr) {
      typespecs = s.MakeTypespecVec();
      m->Typespecs(typespecs);
    }
    writeDataTypes(mod->getDataTypeMap(), m, typespecs, s, false);
    // System elab tasks
    m->Elab_tasks((std::vector<UHDM::tf_call*>*)&mod->getElabSysCalls());
    if (m->Elab_tasks()) {
      for (auto et : *m->Elab_tasks()) {
        et->VpiParent(m);
      }
    }
    // Assertions
    if (mod->getAssertions()) {
      m->Assertions(mod->getAssertions());
      for (auto ps : *m->Assertions()) {
        ps->VpiParent(m);
      }
    }
  }

  writeElabParameters(s, instance, m, exprBuilder);
  if (netlist) {
    m->Ports(netlist->ports());
    if (netlist->ports()) {
      for (auto obj : *netlist->ports()) {
        obj->VpiParent(m);
      }
    }
    m->Nets(netlist->nets());
    if (netlist->nets()) {
      for (auto obj : *netlist->nets()) {
        obj->VpiParent(m);
      }
    }
    m->Gen_scope_arrays(netlist->gen_scopes());
    if (netlist->gen_scopes()) {
      for (auto obj : *netlist->gen_scopes()) {
        obj->VpiParent(m);
      }
    }
    m->Variables(netlist->variables());
    if (netlist->variables()) {
      for (auto obj : *netlist->variables()) {
        obj->VpiParent(m);
      }
    }
    m->Array_vars(netlist->array_vars());
    if (netlist->array_vars()) {
      for (auto obj : *netlist->array_vars()) {
        obj->VpiParent(m);
      }
    }
    m->Named_events(netlist->named_events());
    if (netlist->named_events()) {
      for (auto obj : *netlist->named_events()) {
        obj->VpiParent(m);
      }
    }
    m->Array_nets(netlist->array_nets());
    if (netlist->array_nets()) {
      for (auto obj : *netlist->array_nets()) {
        obj->VpiParent(m);
      }
    }
  }

  if (netlist) {
    if (netlist->cont_assigns()) {
      std::vector<cont_assign*>* assigns = m->Cont_assigns();
      if (assigns == nullptr) {
        m->Cont_assigns(s.MakeCont_assignVec());
        assigns = m->Cont_assigns();
      }
      writeCont_assign(netlist, s, mod, m, assigns);
    }

    // Processes
    m->Process(netlist->process_stmts());
  }

  // Modports
  ModuleDefinition* module = (ModuleDefinition*)mod;
  ModuleDefinition::ModPortSignalMap& orig_modports =
      module->getModPortSignalMap();
  VectorOfmodport* dest_modports = s.MakeModportVec();
  for (auto& orig_modport : orig_modports) {
    modport* dest_modport = s.MakeModport();
    dest_modport->Interface_inst(m);
    dest_modport->VpiName(orig_modport.first);
    dest_modport->VpiParent(m);
    const FileContent* orig_fC = orig_modport.second.getFileContent();
    const NodeId orig_nodeId = orig_modport.second.getNodeId();
    orig_fC->populateCoreMembers(orig_nodeId, orig_nodeId, dest_modport);
    VectorOfio_decl* ios = s.MakeIo_declVec();
    for (auto& sig : orig_modport.second.getPorts()) {
      const FileContent* fC = sig.getFileContent();
      io_decl* io = s.MakeIo_decl();
      io->VpiName(sig.getName());
      NodeId id = sig.getNodeId();
      fC->populateCoreMembers(id, id, io);
      if (NodeId Expression = fC->Sibling(id)) {
        m_helper.checkForLoops(true);
        any* exp =
            m_helper.compileExpression(mod, fC, Expression, m_compileDesign,
                                       Reduce::Yes, io, instance, true);
        m_helper.checkForLoops(false);
        io->Expr(exp);
      }
      uint32_t direction = UhdmWriter::getVpiDirection(sig.getDirection());
      io->VpiDirection(direction);
      io->VpiParent(dest_modport);
      ios->push_back(io);
    }
    dest_modport->Io_decls(ios);
    dest_modports->push_back(dest_modport);
  }
  m->Modports(dest_modports);

  if (mod) {
    // ClockingBlocks
    ModuleDefinition* def = (ModuleDefinition*)mod;
    for (const auto& ctupple : def->getClockingBlockMap()) {
      const ClockingBlock& cblock = ctupple.second;
      ElaboratorContext elaboratorContext(&s, false, true);
      clocking_block* cb = (clocking_block*)UHDM::clone_tree(
          cblock.getActual(), &elaboratorContext);
      cb->VpiParent(m);
      switch (cblock.getType()) {
        case ClockingBlock::Type::Default: {
          m->Default_clocking(cb);
          break;
        }
        case ClockingBlock::Type::Global: {
          m->Global_clocking(cb);
          break;
        }
        case ClockingBlock::Type::Regular: {
          VectorOfclocking_block* cblocks = m->Clocking_blocks();
          if (cblocks == nullptr) {
            m->Clocking_blocks(s.MakeClocking_blockVec());
            cblocks = m->Clocking_blocks();
          }
          cblocks->push_back(cb);
          break;
        }
      }
    }
  }

  if (mod) {
    if (auto from = mod->getTask_funcs()) {
      UHDM::VectorOftask_func* target = m->Task_funcs();
      if (target == nullptr) {
        m->Task_funcs(s.MakeTask_funcVec());
        target = m->Task_funcs();
      }
      for (auto tf : *from) {
        target->push_back(tf);
        if (tf->VpiParent() == nullptr) tf->VpiParent(m);
        if (tf->Instance() == nullptr) tf->Instance(m);
      }
    }
  }
  return true;
}

void writePrimTerms(ModuleInstance* instance, primitive* prim,
                    int32_t vpiGateType, Serializer& s) {
  Netlist* netlist = instance->getNetlist();
  VectorOfprim_term* terms = s.MakePrim_termVec();
  prim->Prim_terms(terms);
  if (netlist->ports()) {
    uint32_t index = 0;
    VectorOfport* ports = netlist->ports();
    for (auto port : *ports) {
      prim_term* term = s.MakePrim_term();
      terms->push_back(term);
      expr* hconn = (expr*)port->High_conn();
      hconn->VpiParent(prim);
      term->Expr(hconn);
      term->VpiFile(port->VpiFile());
      term->VpiLineNo(port->VpiLineNo());
      term->VpiColumnNo(port->VpiColumnNo());
      term->VpiEndLineNo(port->VpiEndLineNo());
      term->VpiEndColumnNo(port->VpiEndColumnNo());
      term->VpiDirection(port->VpiDirection());
      term->VpiParent(prim);
      term->VpiTermIndex(index);
      if (vpiGateType == vpiBufPrim || vpiGateType == vpiNotPrim) {
        if (index < ports->size() - 1) {
          term->VpiDirection(vpiOutput);
        } else {
          term->VpiDirection(vpiInput);
        }
      } else if (vpiGateType == vpiTranif1Prim ||
                 vpiGateType == vpiTranif0Prim ||
                 vpiGateType == vpiRtranif1Prim ||
                 vpiGateType == vpiRtranif0Prim || vpiGateType == vpiTranPrim ||
                 vpiGateType == vpiRtranPrim) {
        if (index < ports->size() - 1) {
          term->VpiDirection(vpiInout);
        } else {
          term->VpiDirection(vpiInput);
        }
      } else {
        if (index == 0) {
          term->VpiDirection(vpiOutput);
        } else {
          term->VpiDirection(vpiInput);
        }
      }
      index++;
    }
  }
}

void UhdmWriter::writeInstance(ModuleDefinition* mod, ModuleInstance* instance,
                               any* m, CompileDesign* compileDesign,
                               ModPortMap& modPortMap, InstanceMap& instanceMap,
                               ExprBuilder& exprBuilder) {
  FileSystem* const fileSystem = FileSystem::getInstance();
  Serializer& s = compileDesign->getSerializer();
  VectorOfmodule_inst* subModules = nullptr;
  VectorOfprogram* subPrograms = nullptr;
  VectorOfinterface_inst* subInterfaces = nullptr;
  VectorOfprimitive* subPrimitives = nullptr;
  VectorOfprimitive_array* subPrimitiveArrays = nullptr;
  VectorOfgen_scope_array* subGenScopeArrays = nullptr;
  m_componentMap.emplace(instance, m);
  if (m->UhdmType() == uhdmmodule_inst) {
    writeElabModule(s, instance, (module_inst*)m, exprBuilder);
  } else if (m->UhdmType() == uhdmgen_scope) {
    writeElabGenScope(s, instance, (gen_scope*)m, exprBuilder);
  } else if (m->UhdmType() == uhdminterface_inst) {
    writeElabInterface(s, instance, (interface_inst*)m, exprBuilder);
  }
  Netlist* netlist = instance->getNetlist();
  if (netlist) {
    if (VectorOfinterface_array* subInterfaceArrays =
            netlist->interface_arrays()) {
      UHDM_OBJECT_TYPE utype = m->UhdmType();
      if (utype == uhdmmodule_inst) {
        ((module_inst*)m)->Interface_arrays(subInterfaceArrays);
        for (interface_array* array : *subInterfaceArrays) {
          array->VpiParent(m);
        }
      } else if (utype == uhdmgen_scope) {
        ((gen_scope*)m)->Interface_arrays(subInterfaceArrays);
        for (interface_array* array : *subInterfaceArrays) {
          array->VpiParent(m);
        }
      } else if (utype == uhdminterface_inst) {
        ((interface_inst*)m)->Interface_arrays(subInterfaceArrays);
        for (interface_array* array : *subInterfaceArrays) {
          array->VpiParent(m);
        }
      }
    }
    if (VectorOfinterface_inst* subInterfaces = netlist->interfaces()) {
      UHDM_OBJECT_TYPE utype = m->UhdmType();
      if (utype == uhdmmodule_inst) {
        ((module_inst*)m)->Interfaces(subInterfaces);
        for (interface_inst* interf : *subInterfaces) {
          interf->VpiParent(m);
        }
      } else if (utype == uhdmgen_scope) {
        ((gen_scope*)m)->Interfaces(subInterfaces);
        for (interface_inst* interf : *subInterfaces) {
          interf->VpiParent(m);
        }
      } else if (utype == uhdminterface_inst) {
        ((interface_inst*)m)->Interfaces(subInterfaces);
        for (interface_inst* interf : *subInterfaces) {
          interf->VpiParent(m);
        }
      }
    }
  }
  std::map<ModuleInstance*, module_inst*> tempInstanceMap;
  for (uint32_t i = 0; i < instance->getNbChildren(); i++) {
    ModuleInstance* child = instance->getChildren(i);
    DesignComponent* childDef = child->getDefinition();
    if (ModuleDefinition* mm =
            valuedcomponenti_cast<ModuleDefinition*>(childDef)) {
      VObjectType insttype = child->getType();
      if (insttype == VObjectType::paModule_instantiation) {
        if (subModules == nullptr) subModules = s.MakeModule_instVec();
        module_inst* sm = s.MakeModule_inst();
        tempInstanceMap.emplace(child, sm);
        instanceMap.emplace(child, sm);
        if (childDef && !childDef->getFileContents().empty() &&
            compileDesign->getCompiler()->isLibraryFile(
                childDef->getFileContents()[0]->getFileId())) {
          sm->VpiCellInstance(true);
        }
        sm->VpiName(child->getInstanceName());
        sm->VpiDefName(child->getModuleName());
        sm->VpiFullName(child->getFullPathName());
        const FileContent* defFile = mm->getFileContents()[0];
        sm->VpiDefFile(fileSystem->toPath(defFile->getFileId()));
        sm->VpiDefLineNo(defFile->Line(mm->getNodeIds()[0]));
        child->getFileContent()->populateCoreMembers(child->getNodeId(),
                                                     child->getNodeId(), sm);
        subModules->push_back(sm);
        if (m->UhdmType() == uhdmmodule_inst) {
          ((module_inst*)m)->Modules(subModules);
          sm->Instance((module_inst*)m);
          sm->Module_inst((module_inst*)m);
          sm->VpiParent(m);
        } else if (m->UhdmType() == uhdmgen_scope) {
          ((gen_scope*)m)->Modules(subModules);
          sm->VpiParent(m);
        }
        writeInstance(mm, child, sm, compileDesign, modPortMap, instanceMap,
                      exprBuilder);
      } else if (insttype == VObjectType::paConditional_generate_construct ||
                 insttype == VObjectType::paLoop_generate_construct ||
                 insttype == VObjectType::paGenerate_begin_end_block ||
                 insttype == VObjectType::paGenerate_item ||
                 insttype == VObjectType::paGenerate_region ||
                 insttype == VObjectType::paGenerate_module_loop_statement ||
                 insttype ==
                     VObjectType::paGenerate_module_conditional_statement ||
                 insttype == VObjectType::paGenerate_module_block ||
                 insttype == VObjectType::paGenerate_module_item ||
                 insttype == VObjectType::paGenerate_module_named_block ||
                 insttype == VObjectType::paGenerate_module_block ||
                 insttype == VObjectType::paGenerate_module_item ||
                 insttype == VObjectType::paGenerate_interface_loop_statement ||
                 insttype ==
                     VObjectType::paGenerate_interface_conditional_statement ||
                 insttype == VObjectType::paGenerate_interface_block ||
                 insttype == VObjectType::paGenerate_interface_item ||
                 insttype == VObjectType::paGenerate_interface_named_block ||
                 insttype == VObjectType::paGenerate_interface_block ||
                 insttype == VObjectType::paGenerate_interface_item) {
        if (subGenScopeArrays == nullptr)
          subGenScopeArrays = s.MakeGen_scope_arrayVec();
        gen_scope_array* sm = s.MakeGen_scope_array();
        sm->VpiName(child->getInstanceName());
        sm->VpiFullName(child->getFullPathName());
        child->getFileContent()->populateCoreMembers(child->getNodeId(),
                                                     child->getNodeId(), sm);
        subGenScopeArrays->push_back(sm);
        gen_scope* a_gen_scope = s.MakeGen_scope();
        sm->Gen_scopes(s.MakeGen_scopeVec());
        sm->Gen_scopes()->push_back(a_gen_scope);
        child->getFileContent()->populateCoreMembers(
            child->getNodeId(), child->getNodeId(), a_gen_scope);
        a_gen_scope->VpiParent(sm);
        UHDM_OBJECT_TYPE utype = m->UhdmType();
        if (utype == uhdmmodule_inst) {
          ((module_inst*)m)->Gen_scope_arrays(subGenScopeArrays);
          sm->VpiParent(m);
        } else if (utype == uhdmgen_scope) {
          ((gen_scope*)m)->Gen_scope_arrays(subGenScopeArrays);
          sm->VpiParent(m);
        } else if (utype == uhdminterface_inst) {
          ((interface_inst*)m)->Gen_scope_arrays(subGenScopeArrays);
          sm->VpiParent(m);
        }
        writeInstance(mm, child, a_gen_scope, compileDesign, modPortMap,
                      instanceMap, exprBuilder);

      } else if (insttype == VObjectType::paInterface_instantiation) {
        if (subInterfaces == nullptr) subInterfaces = s.MakeInterface_instVec();
        interface_inst* sm = s.MakeInterface_inst();
        sm->VpiName(child->getInstanceName());
        sm->VpiDefName(child->getModuleName());
        sm->VpiFullName(child->getFullPathName());
        child->getFileContent()->populateCoreMembers(child->getNodeId(),
                                                     child->getNodeId(), sm);
        const FileContent* defFile = mm->getFileContents()[0];
        sm->VpiDefFile(fileSystem->toPath(defFile->getFileId()));
        sm->VpiDefLineNo(defFile->Line(mm->getNodeIds()[0]));
        subInterfaces->push_back(sm);
        UHDM_OBJECT_TYPE utype = m->UhdmType();
        if (utype == uhdmmodule_inst) {
          ((module_inst*)m)->Interfaces(subInterfaces);
          sm->Instance((module_inst*)m);
          sm->VpiParent(m);
        } else if (utype == uhdmgen_scope) {
          ((gen_scope*)m)->Interfaces(subInterfaces);
          sm->VpiParent(m);
        } else if (utype == uhdminterface_inst) {
          ((interface_inst*)m)->Interfaces(subInterfaces);
          sm->VpiParent(m);
        }
        writeInstance(mm, child, sm, compileDesign, modPortMap, instanceMap,
                      exprBuilder);

      } else if ((insttype == VObjectType::paUdp_instantiation) ||
                 (insttype == VObjectType::paCmos_switch_instance) ||
                 (insttype == VObjectType::paEnable_gate_instance) ||
                 (insttype == VObjectType::paMos_switch_instance) ||
                 (insttype == VObjectType::paN_input_gate_instance) ||
                 (insttype == VObjectType::paN_output_gate_instance) ||
                 (insttype == VObjectType::paPass_enable_switch_instance) ||
                 (insttype == VObjectType::paPass_switch_instance) ||
                 (insttype == VObjectType::paPull_gate_instance)) {
        UHDM::primitive* gate = nullptr;
        UHDM::primitive_array* gate_array = nullptr;
        const FileContent* fC = child->getFileContent();
        NodeId gatenode = fC->Child(fC->Parent(child->getNodeId()));
        VObjectType gatetype = fC->Type(gatenode);
        int32_t vpiGateType = m_helper.getBuiltinType(gatetype);
        if (insttype == VObjectType::paUdp_instantiation) {
          UHDM::udp* udp = s.MakeUdp();
          gate = udp;
          if (ModuleDefinition* mm =
                  valuedcomponenti_cast<ModuleDefinition*>(childDef)) {
            udp->Udp_defn(mm->getUhdmScope<UHDM::udp_defn>());
          }
          if (UHDM::VectorOfrange* ranges = child->getNetlist()->ranges()) {
            gate_array = s.MakeUdp_array();
            VectorOfprimitive* prims = s.MakePrimitiveVec();
            gate_array->Primitives(prims);
            gate_array->Ranges(ranges);
            gate_array->VpiParent(m);
            prims->push_back(gate);
            gate->VpiParent(gate_array);
            for (auto r : *ranges) r->VpiParent(gate_array);
            if (subPrimitiveArrays == nullptr)
              subPrimitiveArrays = s.MakePrimitive_arrayVec();
            subPrimitiveArrays->push_back(gate_array);
          } else {
            if (subPrimitives == nullptr) subPrimitives = s.MakePrimitiveVec();
            subPrimitives->push_back(gate);
          }
        } else if (vpiGateType == vpiPmosPrim || vpiGateType == vpiRpmosPrim ||
                   vpiGateType == vpiNmosPrim || vpiGateType == vpiRnmosPrim ||
                   vpiGateType == vpiCmosPrim || vpiGateType == vpiRcmosPrim ||
                   vpiGateType == vpiTranif1Prim ||
                   vpiGateType == vpiTranif0Prim ||
                   vpiGateType == vpiRtranif1Prim ||
                   vpiGateType == vpiRtranif0Prim ||
                   vpiGateType == vpiTranPrim || vpiGateType == vpiRtranPrim) {
          gate = s.MakeSwitch_tran();
          if (UHDM::VectorOfrange* ranges = child->getNetlist()->ranges()) {
            gate_array = s.MakeSwitch_array();
            VectorOfprimitive* prims = s.MakePrimitiveVec();
            gate_array->Primitives(prims);
            gate_array->Ranges(ranges);
            gate_array->VpiParent(m);
            prims->push_back(gate);
            gate->VpiParent(gate_array);
            for (auto r : *ranges) r->VpiParent(gate_array);
            if (subPrimitiveArrays == nullptr)
              subPrimitiveArrays = s.MakePrimitive_arrayVec();
            subPrimitiveArrays->push_back(gate_array);
          } else {
            if (subPrimitives == nullptr) subPrimitives = s.MakePrimitiveVec();
            subPrimitives->push_back(gate);
          }
          gate->VpiPrimType(vpiGateType);
        } else {
          gate = s.MakeGate();
          if (UHDM::VectorOfrange* ranges = child->getNetlist()->ranges()) {
            gate_array = s.MakeGate_array();
            gate_array->VpiName(child->getInstanceName());
            gate_array->VpiFullName(child->getFullPathName());
            child->getFileContent()->populateCoreMembers(
                child->getNodeId(), child->getNodeId(), gate_array);
            VectorOfprimitive* prims = s.MakePrimitiveVec();
            gate_array->Primitives(prims);
            gate_array->Ranges(ranges);
            gate_array->VpiParent(m);
            prims->push_back(gate);
            gate->VpiParent(gate_array);
            for (auto r : *ranges) r->VpiParent(gate_array);
            if (subPrimitiveArrays == nullptr)
              subPrimitiveArrays = s.MakePrimitive_arrayVec();
            subPrimitiveArrays->push_back(gate_array);
          } else {
            if (subPrimitives == nullptr) subPrimitives = s.MakePrimitiveVec();
            subPrimitives->push_back(gate);
          }
          gate->VpiPrimType(vpiGateType);
        }
        if (UHDM::VectorOfexpr* delays = child->getNetlist()->delays()) {
          if (delays->size() == 1) {
            gate->Delay((*delays)[0]);
          }
        }

        gate->VpiName(child->getInstanceName());
        gate->VpiDefName(child->getModuleName());
        gate->VpiFullName(child->getFullPathName());
        child->getFileContent()->populateCoreMembers(child->getNodeId(),
                                                     child->getNodeId(), gate);
        UHDM_OBJECT_TYPE utype = m->UhdmType();
        if (utype == uhdmmodule_inst) {
          ((module_inst*)m)->Primitives(subPrimitives);
          ((module_inst*)m)->Primitive_arrays(subPrimitiveArrays);
          gate->VpiParent(m);
        } else if (utype == uhdmgen_scope) {
          ((gen_scope*)m)->Primitives(subPrimitives);
          ((gen_scope*)m)->Primitive_arrays(subPrimitiveArrays);
          gate->VpiParent(m);
        }
        writePrimTerms(child, gate, vpiGateType, s);
      } else {
        // Unknown object type
      }
    } else if (Program* prog = valuedcomponenti_cast<Program*>(childDef)) {
      if (subPrograms == nullptr) subPrograms = s.MakeProgramVec();
      program* sm = s.MakeProgram();
      sm->VpiName(child->getInstanceName());
      sm->VpiDefName(child->getModuleName());
      sm->VpiFullName(child->getFullPathName());
      child->getFileContent()->populateCoreMembers(child->getNodeId(),
                                                   child->getNodeId(), sm);
      const FileContent* defFile = prog->getFileContents()[0];
      sm->VpiDefFile(fileSystem->toPath(defFile->getFileId()));
      sm->VpiDefLineNo(defFile->Line(prog->getNodeIds()[0]));
      subPrograms->push_back(sm);
      UHDM_OBJECT_TYPE utype = m->UhdmType();
      if (utype == uhdmmodule_inst) {
        ((module_inst*)m)->Programs(subPrograms);
        sm->Instance((module_inst*)m);
        sm->VpiParent(m);
      } else if (utype == uhdmgen_scope) {
        ((gen_scope*)m)->Programs(subPrograms);
        sm->VpiParent(m);
      }
      writeElabProgram(s, child, sm, modPortMap);
    } else {
      // Undefined module
      if (subModules == nullptr) subModules = s.MakeModule_instVec();
      module_inst* sm = s.MakeModule_inst();
      sm->VpiName(child->getInstanceName());
      sm->VpiDefName(child->getModuleName());
      sm->VpiFullName(child->getFullPathName());
      child->getFileContent()->populateCoreMembers(child->getNodeId(),
                                                   child->getNodeId(), sm);
      subModules->push_back(sm);
      UHDM_OBJECT_TYPE utype = m->UhdmType();
      if (utype == uhdmmodule_inst) {
        ((module_inst*)m)->Modules(subModules);
        sm->Instance((module_inst*)m);
        sm->Module_inst((module_inst*)m);
        sm->VpiParent(m);
      } else if (utype == uhdmgen_scope) {
        ((gen_scope*)m)->Modules(subModules);
        sm->VpiParent(m);
      }
      writeInstance(mm, child, sm, compileDesign, modPortMap, instanceMap,
                    exprBuilder);
    }
  }

  if (m->UhdmType() == uhdmmodule_inst) {
    const auto& moduleArrayModuleInstancesMap =
        instance->getModuleArrayModuleInstancesMap();
    if (!moduleArrayModuleInstancesMap.empty()) {
      ((module_inst*)m)->Module_arrays(s.MakeModule_arrayVec());
      std::vector<UHDM::module_array*> moduleArrays;
      std::transform(
          moduleArrayModuleInstancesMap.begin(),
          moduleArrayModuleInstancesMap.end(), std::back_inserter(moduleArrays),
          [](ModuleInstance::ModuleArrayModuleInstancesMap::const_reference
                 pair) { return pair.first; });
      std::sort(
          moduleArrays.begin(), moduleArrays.end(),
          [](const UHDM::module_array* lhs, const UHDM::module_array* rhs) {
            return lhs->UhdmId() < rhs->UhdmId();
          });
      for (UHDM::module_array* modArray : moduleArrays) {
        const auto& modInstances =
            moduleArrayModuleInstancesMap.find(modArray)->second;
        if (!modInstances.empty()) {
          modArray->Modules(s.MakeModule_instVec());
          modArray->VpiParent(m);
          ((module_inst*)m)->Module_arrays()->push_back(modArray);
          for (ModuleInstance* modInst : modInstances) {
            auto it = tempInstanceMap.find(modInst);
            if (it != tempInstanceMap.end()) {
              modArray->Modules()->push_back(it->second);
            }
          }
        }
      }
    }
  }
}

class AlwaysWithForLoop : public VpiListener {
 public:
  explicit AlwaysWithForLoop() {}
  ~AlwaysWithForLoop() override = default;
  void leaveFor_stmt(const for_stmt* object, vpiHandle handle) override {
    containtsForStmt = true;
  }
  bool containtsForStmt = false;
};

bool alwaysContainsForLoop(Serializer& serializer, any* root) {
  AlwaysWithForLoop* listener = new AlwaysWithForLoop();
  vpiHandle handle = serializer.MakeUhdmHandle(root->UhdmType(), root);
  listener->listenAny(handle);
  vpi_release_handle(handle);
  bool result = listener->containtsForStmt;
  delete listener;
  return result;
}

// synlig has a major problem processing always blocks.
// They are processed mainly in the allModules section which is incorrect in
// some case. They should be processed from the topModules section. Here we try
// to fix temporarily this by filtering out the always blocks containing
// for-loops from the allModules, and those without from the topModules
void filterAlwaysBlocks(Serializer& s, design* d) {
  if (d->AllModules()) {
    for (auto module : *d->AllModules()) {
      if (module->Process()) {
        bool more = true;
        while (more) {
          more = false;
          for (std::vector<process_stmt*>::iterator itr =
                   module->Process()->begin();
               itr != module->Process()->end(); itr++) {
            if ((*itr)->UhdmType() == uhdmalways) {
              if (alwaysContainsForLoop(s, (*itr))) {
                more = true;
                module->Process()->erase(itr);
                break;
              }
            }
          }
        }
      }
    }
  }
  std::queue<scope*> instances;
  if (d->TopModules()) {
    for (auto mod : *d->TopModules()) {
      instances.push(mod);
    }
  }
  while (!instances.empty()) {
    scope* current = instances.front();
    instances.pop();
    if (current->UhdmType() == uhdmmodule_inst) {
      module_inst* mod = (module_inst*)current;
      if (mod->Process()) {
        bool more = true;
        while (more) {
          more = false;
          for (std::vector<process_stmt*>::iterator itr =
                   mod->Process()->begin();
               itr != mod->Process()->end(); itr++) {
            if ((*itr)->UhdmType() == uhdmalways) {
              if (!alwaysContainsForLoop(s, (*itr))) {
                more = true;
                mod->Process()->erase(itr);
                break;
              }
            }
          }
        }
      }
      if (mod->Modules()) {
        for (auto m : *mod->Modules()) {
          instances.push(m);
        }
      }
      if (mod->Gen_scope_arrays()) {
        for (auto m : *mod->Gen_scope_arrays()) {
          instances.push(m->Gen_scopes()->at(0));
        }
      }
    } else if (current->UhdmType() == uhdmgen_scope) {
      gen_scope* sc = (gen_scope*)current;
      if (sc->Modules()) {
        for (auto m : *sc->Modules()) {
          instances.push(m);
        }
      }
      if (sc->Gen_scope_arrays()) {
        for (auto m : *sc->Gen_scope_arrays()) {
          instances.push(m->Gen_scopes()->at(0));
        }
      }
    }
  }
}

bool UhdmWriter::write(PathId uhdmFileId) {
  FileSystem* const fileSystem = FileSystem::getInstance();
  ModPortMap modPortMap;
  InstanceMap instanceMap;
  ModuleMap moduleMap;
  Serializer& s = m_compileDesign->getSerializer();
  ExprBuilder exprBuilder;
  exprBuilder.seterrorReporting(
      m_compileDesign->getCompiler()->getErrorContainer(),
      m_compileDesign->getCompiler()->getSymbolTable());

  Location loc(uhdmFileId);
  Error err(ErrorDefinition::UHDM_CREATING_MODEL, loc);
  m_compileDesign->getCompiler()->getErrorContainer()->addError(err);
  m_compileDesign->getCompiler()->getErrorContainer()->printMessages(
      m_compileDesign->getCompiler()->getCommandLineParser()->muteStdout());

  m_helper.setElaborate(Elaborate::No);
  m_helper.setReduce(Reduce::No);

  // Compute list of design components that are part of the instance tree
  std::set<DesignComponent*> designComponents;
  {
    std::queue<ModuleInstance*> queue;
    for (const auto& pack : m_design->getPackageDefinitions()) {
      if (!pack.second->getFileContents().empty()) {
        if (pack.second->getFileContents()[0] != nullptr)
          designComponents.insert(pack.second);
      }
    }
    for (auto instance : m_design->getTopLevelModuleInstances()) {
      queue.push(instance);
    }

    while (!queue.empty()) {
      ModuleInstance* current = queue.front();
      DesignComponent* def = current->getDefinition();
      queue.pop();
      if (current == nullptr) continue;
      for (ModuleInstance* sub : current->getAllSubInstances()) {
        queue.push(sub);
      }
      const FileContent* fC = current->getFileContent();
      if (fC) {
        designComponents.insert(def);
      }
    }
  }

  std::vector<vpiHandle> designs;
  design* d = nullptr;
  if (m_design) {
    d = m_design->getUhdmDesign();
    vpiHandle designHandle =
        reinterpret_cast<vpiHandle>(new uhdm_handle(uhdmdesign, d));
    std::string designName = "unnamed";
    const auto& topLevelModules = m_design->getTopLevelModuleInstances();
    if (!topLevelModules.empty()) {
      designName = topLevelModules.front()->getModuleName();
    }
    d->VpiName(designName);
    designs.push_back(designHandle);

    // -------------------------------
    // Non-Elaborated Model

    // Packages
    SURELOG::PackageDefinitionVec packages =
        m_design->getOrderedPackageDefinitions();
    for (auto& pack : m_design->getPackageDefinitions()) {
      if (pack.first == "builtin") {
        pack.second->getUhdmScope()->VpiParent(d);
        if (pack.second) packages.insert(packages.begin(), pack.second);
        break;
      }
    }

    if (m_compileDesign->getCompiler()->getCommandLineParser()->elaborate()) {
      m_helper.setElaborate(Elaborate::Yes);
      m_helper.setReduce(Reduce::Yes);

      VectorOfpackage* v2 = s.MakePackageVec();
      d->TopPackages(v2);
      for (Package* pack : packages) {
        if (!pack) continue;
        if (!pack->getFileContents().empty() &&
            pack->getType() == VObjectType::paPackage_declaration) {
          const FileContent* fC = pack->getFileContents()[0];
          package* p = pack->getUhdmScope<package>();
          m_componentMap.emplace(pack, p);
          p->VpiParent(d);
          p->VpiTop(true);
          p->VpiDefName(pack->getName());
          if (pack->Attributes() != nullptr) {
            p->Attributes(pack->Attributes());
            for (auto a : *p->Attributes()) {
              a->VpiParent(p);
            }
          }
          writePackage(pack, p, s, true);
          if (fC) {
            // Builtin package has no file
            const NodeId modId = pack->getNodeIds()[0];
            const NodeId startId =
                fC->sl_collect(modId, VObjectType::paPACKAGE);
            fC->populateCoreMembers(startId, modId, p);
          }
          v2->push_back(p);
        }
      }
    }

    m_helper.setElaborate(Elaborate::No);
    m_helper.setReduce(Reduce::No);

    VectorOfpackage* v3 = s.MakePackageVec();
    d->AllPackages(v3);
    for (Package* pack : packages) {
      if (!pack) continue;
      if (!pack->getFileContents().empty() &&
          pack->getType() == VObjectType::paPackage_declaration) {
        const FileContent* fC = pack->getFileContents()[0];
        package* p = pack->getUnElabPackage()->getUhdmScope<package>();
        m_componentMap.emplace(pack->getUnElabPackage(), p);
        p->VpiName(pack->getName());
        p->VpiParent(d);
        p->VpiDefName(pack->getName());
        if (pack->Attributes() != nullptr) {
          p->Attributes(pack->Attributes());
          for (auto a : *p->Attributes()) {
            a->VpiParent(p);
          }
        }
        v3->push_back(p);
        writePackage(pack->getUnElabPackage(), p, s, false);
        if (fC) {
          // Builtin package has no file
          const NodeId modId = pack->getNodeIds()[0];
          const NodeId startId = fC->sl_collect(modId, VObjectType::paPACKAGE);
          fC->populateCoreMembers(startId, modId, p);
        }
      }
    }

    // Programs
    const auto& programs = m_design->getProgramDefinitions();
    VectorOfprogram* uhdm_programs = s.MakeProgramVec();
    for (const auto& progNamePair : programs) {
      Program* prog = progNamePair.second;
      if (!prog->getFileContents().empty() &&
          prog->getType() == VObjectType::paProgram_declaration) {
        const FileContent* fC = prog->getFileContents()[0];
        program* p = s.MakeProgram();
        m_componentMap.emplace(prog, p);
        moduleMap.emplace(prog->getName(), p);
        p->VpiParent(d);
        p->VpiDefName(prog->getName());
        const NodeId modId = prog->getNodeIds()[0];
        const NodeId startId = fC->sl_collect(modId, VObjectType::paPROGRAM);
        fC->populateCoreMembers(startId, modId, p);
        if (prog->Attributes() != nullptr) {
          p->Attributes(prog->Attributes());
          for (auto a : *p->Attributes()) {
            a->VpiParent(p);
          }
        }
        writeProgram(prog, p, s, modPortMap);
        uhdm_programs->push_back(p);
      }
    }
    d->AllPrograms(uhdm_programs);

    // Interfaces
    const auto& modules = m_design->getModuleDefinitions();
    VectorOfinterface_inst* uhdm_interfaces = s.MakeInterface_instVec();
    for (const auto& modNamePair : modules) {
      ModuleDefinition* mod = modNamePair.second;
      if (mod->getFileContents().empty()) {
        // Built-in primitive
      } else if (mod->getType() == VObjectType::paInterface_declaration) {
        const FileContent* fC = mod->getFileContents()[0];
        interface_inst* m = mod->getUhdmScope<interface_inst>();
        m_componentMap.emplace(mod, m);
        moduleMap.emplace(mod->getName(), m);
        m->VpiParent(d);
        m->VpiDefName(mod->getName());
        const NodeId modId = mod->getNodeIds()[0];
        const NodeId startId = fC->sl_collect(modId, VObjectType::paINTERFACE);
        fC->populateCoreMembers(startId, modId, m);
        if (mod->Attributes() != nullptr) {
          m->Attributes(mod->Attributes());
          for (auto a : *m->Attributes()) {
            a->VpiParent(m);
          }
        }
        uhdm_interfaces->push_back(m);
        writeInterface(mod, m, s, modPortMap);
      }
    }
    d->AllInterfaces(uhdm_interfaces);

    // Modules
    VectorOfmodule_inst* uhdm_modules = s.MakeModule_instVec();
    // Udps
    VectorOfudp_defn* uhdm_udps = s.MakeUdp_defnVec();
    for (const auto& modNamePair : modules) {
      ModuleDefinition* mod = modNamePair.second;
      if (mod->getFileContents().empty()) {
        // Built-in primitive
      } else if (mod->getType() == VObjectType::paModule_declaration) {
        const FileContent* fC = mod->getFileContents()[0];
        module_inst* m = mod->getUhdmScope<module_inst>();
        if (m_compileDesign->getCompiler()->isLibraryFile(
                mod->getFileContents()[0]->getFileId())) {
          m->VpiCellInstance(true);
          // Don't list library cells unused in the design
          if (mod && (designComponents.find(mod) == designComponents.end()))
            continue;
        }
        m_componentMap.emplace(mod, m);
        std::string_view modName = mod->getName();
        moduleMap.emplace(modName, m);
        m->VpiDefName(modName);
        if (modName.find("::") == std::string_view::npos) {
          m->VpiParent(d);
        } else {
          modName = StringUtils::rtrim_until(modName, ':');
          modName.remove_suffix(1);
          ModuleMap::const_iterator pmodIt = moduleMap.find(modName);
          if (pmodIt == moduleMap.end()) {
            m->VpiParent(d);
          } else {
            m->VpiParent(pmodIt->second);
          }
        }
        if (mod->Attributes() != nullptr) {
          m->Attributes(mod->Attributes());
          for (auto a : *m->Attributes()) {
            a->VpiParent(m);
          }
        }
        const NodeId modId = mod->getNodeIds()[0];
        const NodeId startId =
            fC->sl_collect(modId, VObjectType::paModule_keyword);
        fC->populateCoreMembers(startId, modId, m);
        uhdm_modules->push_back(m);
        writeModule(mod->getUnelabMmodule(), m, s, moduleMap, modPortMap);
      } else if (mod->getType() == VObjectType::paUdp_declaration) {
        const FileContent* fC = mod->getFileContents()[0];
        if (UHDM::udp_defn* defn = mod->getUhdmScope<UHDM::udp_defn>()) {
          m_componentMap.emplace(mod, defn);
          defn->VpiParent(d);
          defn->VpiDefName(mod->getName());
          const NodeId modId = mod->getNodeIds()[0];
          const NodeId startId =
              fC->sl_collect(modId, VObjectType::paPRIMITIVE);
          fC->populateCoreMembers(startId, modId, defn);
          if (mod->Attributes() != nullptr) {
            defn->Attributes(mod->Attributes());
            for (auto a : *defn->Attributes()) {
              a->VpiParent(defn);
            }
          }
          uhdm_udps->push_back(defn);
        }
      }
    }
    d->AllModules(uhdm_modules);
    d->AllUdps(uhdm_udps);
    for (module_inst* mod : *uhdm_modules) {
      if (mod->Ref_modules()) {
        for (auto subMod : *mod->Ref_modules()) {
          ModuleMap::iterator itr =
              moduleMap.find(std::string(subMod->VpiDefName()));
          if (itr != moduleMap.end()) {
            subMod->Actual_group((*itr).second);
          }
        }
      }
    }

    // Classes
    const auto& classes = m_design->getClassDefinitions();
    VectorOfclass_defn* v4 = s.MakeClass_defnVec();
    for (const auto& classNamePair : classes) {
      ClassDefinition* classDef = classNamePair.second;
      if (!classDef->getFileContents().empty() &&
          classDef->getType() == VObjectType::paClass_declaration) {
        class_defn* c = classDef->getUhdmScope<class_defn>();
        if (!c->VpiParent()) {
          writeClass(classDef, v4, s, d);
        }
      }
    }
    d->AllClasses(v4);

    // -------------------------------
    // Elaborated Model (Folded)
    if (m_compileDesign->getCompiler()->getCommandLineParser()->elaborate()) {
      m_helper.setElaborate(Elaborate::Yes);
      m_helper.setReduce(Reduce::Yes);

      // Top-level modules
      VectorOfmodule_inst* uhdm_top_modules = s.MakeModule_instVec();
      for (ModuleInstance* inst : topLevelModules) {
        DesignComponent* component = inst->getDefinition();
        ModuleDefinition* mod =
            valuedcomponenti_cast<ModuleDefinition*>(component);
        const auto& itr = m_componentMap.find(mod);
        module_inst* m = s.MakeModule_inst();
        m->VpiTopModule(true);
        m->VpiTop(true);
        module_inst* def = (module_inst*)itr->second;
        m->VpiDefName(def->VpiDefName());
        m->VpiName(def->VpiDefName());  // Top's instance name is module name
        m->VpiFullName(
            def->VpiDefName());  // Top's full instance name is module name
        m->VpiFile(def->VpiFile());
        m->VpiLineNo(def->VpiLineNo());
        m->VpiColumnNo(def->VpiColumnNo());
        m->VpiEndLineNo(def->VpiEndLineNo());
        m->VpiEndColumnNo(def->VpiEndColumnNo());
        writeInstance(mod, inst, m, m_compileDesign, modPortMap, instanceMap,
                      exprBuilder);
        uhdm_top_modules->push_back(m);
      }
      d->TopModules(uhdm_top_modules);
    }
  }

  if (m_compileDesign->getCompiler()->getCommandLineParser()->getUhdmStats()) {
    s.PrintStats(std::cerr, "Non-Elaborated Model");
  }

  m_helper.setElaborate(Elaborate::Yes);
  m_helper.setReduce(Reduce::Yes);

  // ----------------------------------
  // Fully elaborated model
  if (m_compileDesign->getCompiler()->getCommandLineParser()->getElabUhdm()) {
    Error err(ErrorDefinition::UHDM_ELABORATION, loc);
    m_compileDesign->getCompiler()->getErrorContainer()->addError(err);
    m_compileDesign->getCompiler()->getErrorContainer()->printMessages(
        m_compileDesign->getCompiler()->getCommandLineParser()->muteStdout());

    if (ElaboratorContext* elaboratorContext =
            new ElaboratorContext(&s, false, false)) {
      elaboratorContext->m_elaborator.uniquifyTypespec(false);
      elaboratorContext->m_elaborator.listenDesigns(designs);
      delete elaboratorContext;
    }

    bind(s, designs);

    if (m_compileDesign->getCompiler()
            ->getCommandLineParser()
            ->getUhdmStats()) {
      s.PrintStats(std::cerr, "Elaborated Model");
    }

    if (UhdmAdjuster* adjuster = new UhdmAdjuster(&s, d)) {
      adjuster->listenDesigns(designs);
      delete adjuster;
    }
  } else {
    bind(s, designs);
  }

  CommandLineParser* const clp =
      m_compileDesign->getCompiler()->getCommandLineParser();
  if (IntegrityChecker* const checker = new IntegrityChecker(
          fileSystem, clp->getSymbolTable(), clp->getErrorContainer())) {
    for (auto h : designs) {
      const design* const d =
          static_cast<const design*>(((const uhdm_handle*)h)->object);
      checker->listenAny(d);
    }

    delete checker;
    clp->getErrorContainer()->printMessages(clp->muteStdout());
  }

  // ----------------------------------
  // Lint only the elaborated model
  if (UhdmLint* linter = new UhdmLint(&s, d)) {
    linter->listenDesigns(designs);
    delete linter;
  }

  if (m_compileDesign->getCompiler()->getCommandLineParser()->getElabUhdm() &&
      m_compileDesign->getCompiler()
          ->getCommandLineParser()
          ->reportNonSynthesizable()) {
    std::set<const any*> nonSynthesizableObjects;
    if (SynthSubset* annotate =
            new SynthSubset(&s, nonSynthesizableObjects, d, true,
                            m_compileDesign->getCompiler()
                                ->getCommandLineParser()
                                ->reportNonSynthesizableWithFormal())) {
      annotate->listenDesigns(designs);
      annotate->filterNonSynthesizable();
      delete annotate;
      filterAlwaysBlocks(s, d);
    }
  }

  // Purge obsolete typespecs
  for (auto o : m_compileDesign->getSwapedObjects()) {
    const typespec* orig = o.first;
    const typespec* tps = o.second;
    if (tps != orig) s.Erase(orig);
  }

  const fs::path uhdmFile = fileSystem->toPlatformAbsPath(uhdmFileId);
  if (m_compileDesign->getCompiler()->getCommandLineParser()->writeUhdm()) {
    Error err(ErrorDefinition::UHDM_WRITE_DB, loc);
    m_compileDesign->getCompiler()->getErrorContainer()->addError(err);
    m_compileDesign->getCompiler()->getErrorContainer()->printMessages(
        m_compileDesign->getCompiler()->getCommandLineParser()->muteStdout());
    s.SetGCEnabled(
        m_compileDesign->getCompiler()->getCommandLineParser()->gc());
    s.Save(uhdmFile);
  }

  if (m_compileDesign->getCompiler()->getCommandLineParser()->getDebugUhdm() ||
      m_compileDesign->getCompiler()->getCommandLineParser()->getCoverUhdm()) {
    // Check before restore
    Location loc(fileSystem->getCheckerHtmlFile(
        uhdmFileId, m_compileDesign->getCompiler()->getSymbolTable()));
    Error err(ErrorDefinition::UHDM_WRITE_HTML_COVERAGE, loc);
    m_compileDesign->getCompiler()->getErrorContainer()->addError(err);
    m_compileDesign->getCompiler()->getErrorContainer()->printMessages(
        m_compileDesign->getCompiler()->getCommandLineParser()->muteStdout());

    if (UhdmChecker* uhdmchecker = new UhdmChecker(m_compileDesign, m_design)) {
      uhdmchecker->check(uhdmFileId);
      delete uhdmchecker;
    }
  }

  if (m_compileDesign->getCompiler()->getCommandLineParser()->getDebugUhdm()) {
    Location loc(
        m_compileDesign->getCompiler()->getSymbolTable()->registerSymbol(
            "in-memory uhdm"));
    Error err2(ErrorDefinition::UHDM_VISITOR, loc);
    m_compileDesign->getCompiler()->getErrorContainer()->addError(err2);
    m_compileDesign->getCompiler()->getErrorContainer()->printMessages(
        m_compileDesign->getCompiler()->getCommandLineParser()->muteStdout());
    std::cout << "====== UHDM =======\n";
    vpi_show_ids(
        m_compileDesign->getCompiler()->getCommandLineParser()->showVpiIds());
    visit_designs(designs, std::cout);
    std::cout << "===================\n";
  }
  m_compileDesign->getCompiler()->getErrorContainer()->printMessages(
      m_compileDesign->getCompiler()->getCommandLineParser()->muteStdout());
  return true;
}
}  // namespace SURELOG
