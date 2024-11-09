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
 * File:   FileContent.h
 * Author: alain
 *
 * Created on June 8, 2017, 8:22 PM
 */

#ifndef SURELOG_FILECONTENT_H
#define SURELOG_FILECONTENT_H
#pragma once

#include <set>
#include <map>
#include <string_view>
#include <string>
#include <Surelog/Common/Containers.h>
#include <Surelog/Common/NodeId.h>
#include <Surelog/Design/DesignComponent.h>
#include <Surelog/Design/VObject.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SURELOG {

class ClassDefinition;
class ErrorContainer;
class ExprBuilder;
class Library;
class ModuleDefinition;
class Package;
class Program;

class FileContent final : public DesignComponent {
  SURELOG_IMPLEMENT_RTTI(FileContent, DesignComponent)
 public:
  FileContent(PathId fileId, Library* library, SymbolTable* symbolTable,
              ErrorContainer* errors, FileContent* parent, PathId fileChunkId);
  ~FileContent() final;

  void setLibrary(Library* lib) { m_library = lib; }

  typedef std::map<std::string, NodeId, std::less<>> NameIdMap;

  NodeId sl_get(NodeId parent,
                VObjectType type) const;  // Get first child item of type

  NodeId sl_parent(NodeId parent,
                   VObjectType type) const;  // Get first parent item of type

  NodeId sl_parent(
      NodeId parent, const VObjectTypeUnorderedSet& types,
      VObjectType& actualType) const;  // Get first parent item of type

  std::vector<NodeId> sl_get_all(
      NodeId parent, VObjectType type) const;  // get all child items of type

  std::vector<NodeId> sl_get_all(NodeId parent,
                                 const VObjectTypeUnorderedSet& types)
      const;  // get all child items of types

  NodeId sl_collect(
      NodeId parent,
      VObjectType type) const;  // Recursively search for first item of type

  NodeId sl_collect(
      NodeId parent, VObjectType type,
      VObjectType stopType) const;  // Recursively search for first item of type

  std::vector<NodeId> sl_collect_all(
      NodeId parent, VObjectType type,
      bool first = false) const;  // Recursively search for all items of type

  std::vector<NodeId> sl_collect_all(
      NodeId parent, const VObjectTypeUnorderedSet& types,
      bool first = false) const;  // Recursively search for all items of types

  std::vector<NodeId> sl_collect_all(NodeId parent,
                                     const VObjectTypeUnorderedSet& types,
                                     const VObjectTypeUnorderedSet& stopPoints,
                                     bool first = false) const;
  // Recursively search for all items of types
  // and stops at types stopPoints
  uint32_t getSize() const final {
    return static_cast<uint32_t>(m_objects.size());
  }
  VObjectType getType() const final { return VObjectType::slNoType; }
  bool isInstance() const final { return false; }
  std::string_view getName() const final;
  NodeId getRootNode() const;
  std::string printObjects() const;  // The whole file content
  std::string printSubTree(
      NodeId parentIndex) const;                 // Print subtree from parent
  std::string printObject(NodeId noedId) const;  // Only print that object
  std::vector<std::string> collectSubTree(
      NodeId uniqueId) const;  // Helper function
  SymbolTable* getSymbolTable() const { return m_symbolTable; }
  void setSymbolTable(SymbolTable* table) { m_symbolTable = table; }
  PathId getFileId(NodeId id) const;
  PathId* getMutableFileId(NodeId id);
  Library* getLibrary() const { return m_library; }
  std::vector<DesignElement*>& getDesignElements() { return m_elements; }
  const std::vector<DesignElement*>& getDesignElements() const { return m_elements; }
  void addDesignElement(std::string_view name, DesignElement* elem);
  const DesignElement* getDesignElement(std::string_view name) const;
  using DesignComponent::addObject;
  NodeId addObject(SymbolId name, PathId fileId, VObjectType type,
                   uint32_t line, uint16_t column,
                   uint32_t endLine, uint16_t endColumn,
                   NodeId parent = InvalidNodeId,
                   NodeId definition = InvalidNodeId,
                   NodeId child = InvalidNodeId,
                   NodeId sibling = InvalidNodeId);
  const std::vector<VObject>& getVObjects() const { return m_objects; }
  std::vector<VObject>* mutableVObjects() { return &m_objects; }
  const NameIdMap& getObjectLookup() const { return m_objectLookup; }
  void insertObjectLookup(std::string_view name, NodeId id,
                          ErrorContainer* errors);
  std::set<std::string, std::less<>>& getReferencedObjects() {
    return m_referencedObjects;
  }

  const VObject& Object(NodeId index) const;
  VObject* MutableObject(NodeId index);

  NodeId UniqueId(NodeId index) const;

  SymbolId Name(NodeId index) const;

  NodeId Child(NodeId index) const;

  NodeId Sibling(NodeId index) const;

  NodeId Definition(NodeId index) const;

  void SetDefinitionFile(NodeId index, PathId def);
  PathId GetDefinitionFile(NodeId index) const;

  NodeId Parent(NodeId index) const;

  VObjectType Type(NodeId index) const;

  uint32_t Line(NodeId index) const;

  uint16_t Column(NodeId index) const;

  uint32_t EndLine(NodeId index) const;

  uint16_t EndColumn(NodeId index) const;

  std::string_view SymName(NodeId index) const;

  const ModuleNameModuleDefinitionMap& getModuleDefinitions() const {
    return m_moduleDefinitions;
  }
  const PackageNamePackageDefinitionMultiMap& getPackageDefinitions() const {
    return m_packageDefinitions;
  }
  const ProgramNameProgramDefinitionMap& getProgramDefinitions() const {
    return m_programDefinitions;
  }
  const ClassNameClassDefinitionMultiMap& getClassDefinitions() const {
    return m_classDefinitions;
  }
  void addModuleDefinition(std::string_view moduleName, ModuleDefinition* def) {
    m_moduleDefinitions.emplace(moduleName, def);
  }
  void addPackageDefinition(std::string_view packageName, Package* package) {
    m_packageDefinitions.emplace(packageName, package);
  }
  void addProgramDefinition(std::string_view programName, Program* program) {
    m_programDefinitions.emplace(programName, program);
  }
  void addClassDefinition(std::string_view className,
                          ClassDefinition* classDef) {
    m_classDefinitions.emplace(className, classDef);
  }

  const ModuleDefinition* getModuleDefinition(
      std::string_view moduleName) const;

  DesignComponent* getComponentDefinition(std::string_view componentName) const;

  Package* getPackage(std::string_view name) const;

  const Program* getProgram(std::string_view name) const;

  const ClassDefinition* getClassDefinition(std::string_view name) const;

  const FileContent* getParent() const { return m_parentFile; }
  void setParent(FileContent* parent) { m_parentFile = parent; }

  bool diffTree(NodeId id, const FileContent* oFc, NodeId oId,
                std::string* diff_out) const;

  PathId getFileId() const { return m_fileId; }
  PathId getChunkFileId() const { return m_fileChunkId; }

  bool isLibraryCellFile() const { return m_isLibraryCellFile; }
  void setLibraryCellFile() { m_isLibraryCellFile = true; }

  void populateCoreMembers(NodeId startIndex, NodeId endIndex,
                           UHDM::any* instance) const;

 protected:
  std::vector<DesignElement*> m_elements;
  std::map<std::string, DesignElement*, StringViewCompare> m_elementMap;
  std::vector<VObject> m_objects;
  std::unordered_map<NodeId, PathId, NodeIdHasher, NodeIdEqualityComparer>
      m_definitionFiles;

  NameIdMap m_objectLookup;  // Populated at ResolveSymbol stage
  std::set<std::string, std::less<>> m_referencedObjects;

  ModuleNameModuleDefinitionMap m_moduleDefinitions;

  PackageNamePackageDefinitionMultiMap m_packageDefinitions;

  ProgramNameProgramDefinitionMap m_programDefinitions;

  ClassNameClassDefinitionMultiMap m_classDefinitions;

  const PathId m_fileId;
  const PathId m_fileChunkId;
  ErrorContainer* const m_errors;

  Library* m_library;          // TODO: should be set in constructor and *const
  SymbolTable* m_symbolTable;  // TODO: should be set in constructor *const
  FileContent* m_parentFile;   // for file chunks
  bool m_isLibraryCellFile = false;
};

};  // namespace SURELOG

#endif /* SURELOG_FILECONTENT_H */
