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
 * File:   ResolveSymbols.h
 * Author: alain
 *
 * Created on July 1, 2017, 12:38 PM
 */

#ifndef SURELOG_RESOLVESYMBOLS_H
#define SURELOG_RESOLVESYMBOLS_H
#pragma once

#include <Surelog/Common/SymbolId.h>
#include <Surelog/DesignCompile/CompileStep.h>

#include <unordered_set>

namespace SURELOG {
class CompileDesign;
class Compiler;
class Design;
class FileContent;
class Session;

struct FunctorCreateLookup final {
  FunctorCreateLookup(Session* session, CompileDesign* compileDesign,
                      FileContent* fileContent, Design* design)
      : m_session(session),
        m_compileDesign(compileDesign),
        m_fileContent(fileContent) {}

  int32_t operator()() const;

 private:
  Session* const m_session = nullptr;
  CompileDesign* const m_compileDesign = nullptr;
  FileContent* const m_fileContent = nullptr;
};

struct FunctorResolve final {
  FunctorResolve(Session* session, CompileDesign* compileDesign,
                 FileContent* fileContent, Design* design)
      : m_session(session),
        m_compileDesign(compileDesign),
        m_fileContent(fileContent) {}

  int32_t operator()() const;

 private:
  Session* const m_session = nullptr;
  CompileDesign* const m_compileDesign = nullptr;
  FileContent* const m_fileContent = nullptr;
};

class ResolveSymbols : public CompileStep {
 public:
  ResolveSymbols(Session* session, CompileDesign* compileDesign,
                 FileContent* fileContent)
      : m_session(session),
        m_compileDesign(compileDesign),
        m_fileContent(fileContent) {}

  void createFastLookup();

  bool resolve();

  VObject Object(NodeId index) const override;
  VObject* MutableObject(NodeId index);

  NodeId UniqueId(NodeId index) const override;

  SymbolId Name(NodeId index) const override;

  NodeId Child(NodeId index) const override;

  NodeId Sibling(NodeId index) const override;

  NodeId Definition(NodeId index) const override;
  bool SetDefinition(NodeId index, NodeId node);

  NodeId Parent(NodeId index) const override;

  VObjectType Type(NodeId index) const override;
  bool SetType(NodeId index, VObjectType type);

  uint32_t Line(NodeId index) const override;

  std::string_view Symbol(SymbolId id) const override;

  std::string_view SymName(NodeId index) const override;

  NodeId sl_get(NodeId parent,
                VObjectType type) const override;  // Get first item of type

  NodeId sl_parent(
      NodeId parent,
      VObjectType type) const override;  // Get first parent item of type

  NodeId sl_parent(NodeId parent, const VObjectTypeUnorderedSet& types,
                   VObjectType& actualType) const override;

  std::vector<NodeId> sl_get_all(
      NodeId parent,
      VObjectType type) const override;  // get all items of type

  NodeId sl_collect(NodeId parent,
                    VObjectType type)
      const override;  // Recursively search for first item of type

  std::vector<NodeId> sl_collect_all(NodeId parent,
                                     VObjectType type)
      const override;  // Recursively search for all items of type

  ResolveSymbols(const ResolveSymbols& orig);
  ~ResolveSymbols() override = default;

  Compiler* getCompiler() const;

 private:
  bool bindDefinition_(NodeId objIndex,
                       const VObjectTypeUnorderedSet& bindTypes);

  Session* const m_session = nullptr;
  CompileDesign* const m_compileDesign = nullptr;
  FileContent* const m_fileContent = nullptr;
};

};  // namespace SURELOG

#endif /* SURELOG_RESOLVESYMBOLS_H */
