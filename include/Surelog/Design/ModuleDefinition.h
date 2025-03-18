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
 * File:   ModuleDefinition.h
 * Author: alain
 *
 * Created on October 20, 2017, 10:29 PM
 */

#ifndef SURELOG_MODULEDEFINITION_H
#define SURELOG_MODULEDEFINITION_H
#pragma once

#include <Surelog/Common/ClockingBlockHolder.h>
#include <Surelog/Common/Containers.h>
#include <Surelog/Common/SymbolId.h>
#include <Surelog/Design/DesignComponent.h>
#include <Surelog/Design/Modport.h>

// UHDM
#include <uhdm/containers.h>

#include <string_view>
#include <vector>

namespace uhdm {
class Serializer;
}

namespace SURELOG {

class CompileModule;
class FileContent;

class ModuleDefinition : public DesignComponent, public ClockingBlockHolder {
  SURELOG_IMPLEMENT_RTTI(ModuleDefinition, DesignComponent)
  friend CompileModule;

 public:
  ModuleDefinition(Session* session, std::string_view name,
                   const FileContent* fileContent, NodeId nodeId,
                   uhdm::Serializer& serializer);
  ~ModuleDefinition() override = default;

  std::string_view getName() const override { return m_name; }
  VObjectType getType() const override;
  bool isInstance() const override;
  uint32_t getSize() const override;

  typedef std::map<std::string, ClockingBlock> ClockingBlockMap;
  typedef std::map<std::string, Modport, std::less<>> ModportSignalMap;
  typedef std::map<std::string, std::vector<ClockingBlock>, std::less<>>
      ModportClockingBlockMap;

  ModportSignalMap& getModportSignalMap() { return m_modportSignalMap; }
  ModportClockingBlockMap& getModportClockingBlockMap() {
    return m_modportClockingBlockMap;
  }
  void insertModport(std::string_view modport, const Signal& signal,
                     NodeId nodeId);
  void insertModport(std::string_view modport, ClockingBlock& block);
  const Signal* getModportSignal(std::string_view modport, NodeId port) const;
  Modport* getModport(std::string_view modport);

  const ClockingBlock* getModportClockingBlock(std::string_view modport,
                                               NodeId port) const;

  ClassNameClassDefinitionMultiMap& getClassDefinitions() {
    return m_classDefinitions;
  }
  void addClassDefinition(std::string_view className,
                          ClassDefinition* classDef) {
    m_classDefinitions.emplace(className, classDef);
  }
  ClassDefinition* getClassDefinition(std::string_view name);

  void setGenBlockId(NodeId id) {
    m_genBlockId = id;
    if (m_unelabModule != this) m_unelabModule->setGenBlockId(id);
  }

  NodeId getGenBlockId() const { return m_genBlockId; }

  uhdm::AttributeCollection* getAttributes() const { return attributes_; }

  bool setAttributes(uhdm::AttributeCollection* data) {
    attributes_ = data;
    return true;
  }
  std::vector<uhdm::ModuleArray*>* getModuleArrays() { return m_moduleArrays; }
  void setModuleArrays(std::vector<uhdm::ModuleArray*>* modules) {
    m_moduleArrays = modules;
  }

  std::vector<uhdm::RefModule*>* getRefModules() { return m_refModules; }
  void setRefModules(std::vector<uhdm::RefModule*>* modules) {
    m_refModules = modules;
  }

  uhdm::PrimitiveCollection* getPrimitives() { return m_subPrimitives; }
  uhdm::PrimitiveArrayCollection* getPrimitiveArrays() {
    return m_subPrimitiveArrays;
  }
  uhdm::GenScopeArrayCollection* getGenScopeArrays() {
    return m_subGenScopeArrays;
  }
  std::vector<uhdm::Any*>* getGenStmts() { return m_genStmts; }
  void setPrimitives(uhdm::PrimitiveCollection* primitives) {
    m_subPrimitives = primitives;
  }
  void setPrimitiveArrays(uhdm::PrimitiveArrayCollection* primitives) {
    m_subPrimitiveArrays = primitives;
  }
  void setGenScopeArrays(uhdm::GenScopeArrayCollection* gen_arrays) {
    m_subGenScopeArrays = gen_arrays;
  }
  void setGenStmts(std::vector<uhdm::Any*>* gen_stmts) {
    m_genStmts = gen_stmts;
  }
  std::string_view getEndLabel() const { return m_endLabel; }
  void setEndLabel(std::string_view endLabel) { m_endLabel = endLabel; }

  ModuleDefinition* getUnelabMmodule() { return m_unelabModule; }

 private:
  std::string m_name;
  std::string m_endLabel;
  ModportSignalMap m_modportSignalMap;
  ModportClockingBlockMap m_modportClockingBlockMap;
  ClassNameClassDefinitionMultiMap m_classDefinitions;
  NodeId m_genBlockId;
  ModuleDefinition* m_unelabModule = nullptr;
  uhdm::UdpDefn* m_udpDefn = nullptr;

  uhdm::AttributeCollection* attributes_ = nullptr;
  std::vector<uhdm::ModuleArray*>* m_moduleArrays = nullptr;
  std::vector<uhdm::RefModule*>* m_refModules = nullptr;
  uhdm::PrimitiveCollection* m_subPrimitives = nullptr;
  uhdm::PrimitiveArrayCollection* m_subPrimitiveArrays = nullptr;
  uhdm::GenScopeArrayCollection* m_subGenScopeArrays = nullptr;
  std::vector<uhdm::Any*>* m_genStmts = nullptr;
};

};  // namespace SURELOG

#endif /* SURELOG_MODULEDEFINITION_H */
