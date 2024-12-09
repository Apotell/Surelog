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
 * File:   ObjectBinder.h
 * Author: hs
 *
 * Created on August 10, 2024, 00:00 AM
 */

#ifndef SURELOG_OBJECTBINDER_H
#define SURELOG_OBJECTBINDER_H
#pragma once

#include <Surelog/SourceCompile/VObjectTypes.h>

// uhdm
#include <uhdm/UhdmListener.h>
#include <uhdm/uhdm_forward_decl.h>

#include <map>
#include <set>
#include <string_view>
#include <vector>

namespace UHDM {
class Serializer;
}  // namespace UHDM

namespace SURELOG {

class Session;
class ValuedComponentI;

class ObjectBinder final : protected UHDM::UhdmListener {
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
  ObjectBinder(Session* session, const ComponentMap& componentMap,
               UHDM::Serializer& serializer, bool muteStdout);

  void bind(const UHDM::design* const object, bool report);
  void bind(const std::vector<const UHDM::design*>& objects, bool report);

 private:
  void enterDesign(const UHDM::design* const object) final;
  void leaveDesign(const UHDM::design* const object) final;

  void enterPackage(const UHDM::package* const object) final;
  void leavePackage(const UHDM::package* const object) final;

  void enterHier_path(const UHDM::hier_path* const object) final;
  void leaveHier_path(const UHDM::hier_path* const object) final;

  void enterBit_select(const UHDM::bit_select* const object) final;
  void enterVar_select(const UHDM::var_select* const object) final;

  // void enterChandle_var(const UHDM::chandle_var* const object) final;

  void enterIndexed_part_select(
      const UHDM::indexed_part_select* const object) final;

  void enterPart_select(const UHDM::part_select* const object) final;

  void enterRef_module(const UHDM::ref_module* const object) final;

  void enterRef_obj(const UHDM::ref_obj* const object) final;

  void enterRef_typespec(const UHDM::ref_typespec* const object) final;

  void enterClass_defn(const UHDM::class_defn* const object) final;

 private:
  bool areSimilarNames(std::string_view name1, std::string_view name2) const;
  bool areSimilarNames(const UHDM::any* const object1,
                       std::string_view name2) const;
  bool areSimilarNames(const UHDM::any* const object1,
                       const UHDM::any* const object2) const;
  static bool isInElaboratedTree(const UHDM::any* const object);

  VObjectType getDefaultNetType(const ValuedComponentI* component) const;
  const UHDM::any* getPrefix(const UHDM::any* const object);

  const UHDM::package* getPackage(std::string_view name) const;
  const UHDM::module_inst* getModuleInst(std::string_view defname) const;
  const UHDM::interface_inst* getInterfaceInst(std::string_view defname) const;

  const UHDM::class_defn* getClass_defn(
      const UHDM::VectorOfclass_defn* const collection,
      std::string_view name) const;
  const UHDM::class_defn* getClass_defn(std::string_view name) const;

  const UHDM::any* findInTypespec(std::string_view name,
                                  const UHDM::any* const object,
                                  const UHDM::typespec* const scope);
  const UHDM::any* findInRefTypespec(std::string_view name,
                                     const UHDM::any* const object,
                                     const UHDM::ref_typespec* const scope);
  template <typename T>
  const UHDM::any* findInVectorOfAny(std::string_view name,
                                     const UHDM::any* const object,
                                     const std::vector<T*>* const collection,
                                     const UHDM::any* const scope);
  const UHDM::any* findInScope(std::string_view name,
                               const UHDM::any* const object,
                               const UHDM::scope* const scope);
  const UHDM::any* findInInstance(std::string_view name,
                                  const UHDM::any* const object,
                                  const UHDM::instance* const scope);
  const UHDM::any* findInInterface_inst(
      std::string_view name, const UHDM::any* const object,
      const UHDM::interface_inst* const scope);
  const UHDM::any* findInPackage(std::string_view name,
                                 const UHDM::any* const object,
                                 const UHDM::package* const scope);
  const UHDM::any* findInUdp_defn(std::string_view name,
                                  const UHDM::any* const object,
                                  const UHDM::udp_defn* const scope);
  const UHDM::any* findInProgram(std::string_view name,
                                 const UHDM::any* const object,
                                 const UHDM::program* const scope);
  const UHDM::any* findInFunction(std::string_view name,
                                  const UHDM::any* const object,
                                  const UHDM::function* const scope);
  const UHDM::any* findInTask(std::string_view name,
                              const UHDM::any* const object,
                              const UHDM::task* const scope);
  const UHDM::any* findInFor_stmt(std::string_view name,
                                  const UHDM::any* const object,
                                  const UHDM::for_stmt* const scope);
  const UHDM::any* findInForeach_stmt(std::string_view name,
                                      const UHDM::any* const object,
                                      const UHDM::foreach_stmt* const scope);
  template <typename T>
  const UHDM::any* findInScope_sub(
      std::string_view name, const UHDM::any* const object,
      const T* const scope,
      typename std::enable_if<
          std::is_same<UHDM::begin, typename std::decay<T>::type>::value ||
          std::is_same<UHDM::fork_stmt,
                       typename std::decay<T>::type>::value>::type* = 0);
  const UHDM::any* findInClass_defn(std::string_view name,
                                    const UHDM::any* const object,
                                    const UHDM::class_defn* const scope);
  const UHDM::any* findInModule_inst(std::string_view name,
                                     const UHDM::any* const object,
                                     const UHDM::module_inst* const scope);
  const UHDM::any* findInDesign(std::string_view name,
                                const UHDM::any* const object,
                                const UHDM::design* const scope);

  const UHDM::any* bindObject(const UHDM::any* const object);

  void reportErrors();
  bool createDefaultNets();

 private:
  Session* const m_session = nullptr;
  const ComponentMap& m_componentMap;
  UHDM::Serializer& m_serializer;
  const bool m_muteStdout = false;

  BaseClassMap m_baseclassMap;
  PrefixStack m_prefixStack;
  DesignStack m_designStack;
  PackageStack m_packageStack;
  Unbounded m_unbounded;
  Searched m_searched;
};

};  // namespace SURELOG

#endif /* SURELOG_OBJECTBINDER_H */
