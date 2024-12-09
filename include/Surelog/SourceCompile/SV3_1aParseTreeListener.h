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
 * File:   SV3_1aParseTreeListener.h
 * Author: hs
 *
 * Created on January 31, 2023, 12:00 PM
 */

#ifndef SURELOG_SV3_1APARSETREELISTENER_H
#define SURELOG_SV3_1APARSETREELISTENER_H
#pragma once

#include <Surelog/SourceCompile/SV3_1aTreeShapeHelper.h>
#include <parser/SV3_1aParserBaseListener.h>

#include <map>
#include <optional>
#include <regex>
#include <tuple>
#include <vector>

namespace SURELOG {
class Session;

class SV3_1aParseTreeListener final : public SV3_1aParserBaseListener,
                                      public SV3_1aTreeShapeHelper {
  typedef std::vector<VObject> vobjects_t;

  typedef std::vector<antlr4::ParserRuleContext*> rule_callstack_t;
  typedef std::set<const antlr4::Token*> visited_tokens_t;
  typedef std::multimap<antlr4::ParserRuleContext*, NodeId> orphan_objects_t;
  typedef std::vector<antlr4::Token*> preproc_begin_statck_t;

  typedef std::tuple<uint16_t, int32_t> column_offset_t;
  typedef std::multimap<uint32_t, column_offset_t> offsets_t;

 public:
  SV3_1aParseTreeListener(Session* session, ParseFile* pf,
                          antlr4::CommonTokenStream* tokens,
                          uint32_t lineOffset, FileContent* ppFileContent);
  ~SV3_1aParseTreeListener() final = default;

  void enterString_value(SV3_1aParser::String_valueContext* ctx) final;

  void enterEveryRule(antlr4::ParserRuleContext* ctx) final;
  void exitEveryRule(antlr4::ParserRuleContext* ctx) final;
  void visitTerminal(antlr4::tree::TerminalNode* node) final;
  void visitErrorNode(antlr4::tree::ErrorNode* node) final;

 private:
  using SV3_1aTreeShapeHelper::addVObject;
  NodeId addVObject(antlr4::ParserRuleContext* ctx, antlr4::Token* token,
                    VObjectType objectType);
  NodeId addVObject(antlr4::tree::TerminalNode* node, VObjectType objectType);

  NodeId mergeObjectTree(NodeId ppNodeId);
  std::optional<bool> isUnaryOperator(
      const antlr4::tree::TerminalNode* node) const;

  void sortChildren(vobjects_t& objects, NodeId id) const;
  void applyLocationOffsets();
  void visitPreprocBegin(antlr4::Token* token);
  void visitPreprocEnd(antlr4::Token* token, NodeId ppNodeId);
  void processPendingTokens(antlr4::ParserRuleContext* ctx,
                            size_t endTokenIndex);
  void processOrphanObjects(antlr4::ParserRuleContext* ctx, NodeId parentId);

  typedef std::map<uint32_t, std::pair<uint16_t, uint16_t>> line_ends_t;
  void collectLineRanges(NodeId ppParentId, line_ends_t &ends) const;
  void collectLineRanges(const antlr4::Token* begin, const antlr4::Token* end,
                         line_ends_t& ends) const;

 private:
  FileContent* const m_ppFileContent = nullptr;
  offsets_t m_offsets;

  rule_callstack_t m_ruleCallstack;
  visited_tokens_t m_visitedTokens;
  orphan_objects_t m_orphanObjects;
  preproc_begin_statck_t m_preprocBeginStack;

  size_t m_lastVisitedTokenIndex = 0;
  int32_t m_paused = 0;
};
}  // namespace SURELOG
#endif  // SURELOG_SV3_1APARSETREELISTENER_H
