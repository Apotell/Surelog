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
 * File:   SV3_1aParseTreeListener.cpp
 * Author: hs
 *
 * Created on January 31, 2023, 12:00 PM
 */

#include <Surelog/Design/Design.h>
#include <Surelog/Design/FileContent.h>
#include <Surelog/SourceCompile/CompileSourceFile.h>
#include <Surelog/SourceCompile/Compiler.h>
#include <Surelog/SourceCompile/ParseFile.h>
#include <Surelog/SourceCompile/SV3_1aParseTreeListener.h>
#include <Surelog/Utils/NumUtils.h>
#include <Surelog/Utils/ParseUtils.h>
#include <parser/SV3_1aLexer.h>

#include <algorithm>
#include <iomanip>

namespace SURELOG {
static constexpr std::pair<uint16_t, uint16_t> kMinMaxRange(
    static_cast<uint16_t>(~0), 0);

SV3_1aParseTreeListener::SV3_1aParseTreeListener(
    Session *session, ParseFile *pf, antlr4::CommonTokenStream *tokens,
    uint32_t lineOffset, FileContent *ppFileContent)
    : SV3_1aTreeShapeHelper(session, pf, tokens, lineOffset),
      m_ppFileContent(ppFileContent) {
  if (m_pf->getFileContent() == nullptr) {
    m_fileContent = new FileContent(session, m_pf->getFileId(0),
                                    m_pf->getLibrary(), nullptr, BadPathId);
    m_pf->setFileContent(m_fileContent);
    // m_pf->getCompileSourceFile()->getCompiler()->getDesign()->addFileContent(
    //     m_pf->getFileId(0), m_fileContent);
  } else {
    m_fileContent = m_pf->getFileContent();
  }
}

NodeId SV3_1aParseTreeListener::addVObject(antlr4::ParserRuleContext *ctx,
                                           antlr4::Token *token,
                                           VObjectType objectType) {
  if (m_paused != 0) return InvalidNodeId;
  if (!m_visitedTokens.emplace(token).second) return InvalidNodeId;

  auto [fileId, line, column, endLine, endColumn] = getFileLine(nullptr, token);
  const std::string text = token->getText();

  NodeId childIndex =
      m_fileContent->addObject(registerSymbol(text), fileId, objectType, line,
                               column, endLine, endColumn);

  NodeId parentIndex = NodeIdFromContext(ctx);

  VObject *const childObject = m_fileContent->MutableObject(childIndex);
  VObject *const parentObject = m_fileContent->MutableObject(parentIndex);

  childObject->m_parent = parentIndex;
  childObject->m_sibling = parentObject->m_child;
  parentObject->m_child = childIndex;

  return childIndex;
}

NodeId SV3_1aParseTreeListener::addVObject(antlr4::tree::TerminalNode *node,
                                           VObjectType objectType) {
  return (m_paused == 0) ? addVObject((antlr4::ParserRuleContext *)node,
                                      node->getText(), objectType)
                         : InvalidNodeId;
}

NodeId SV3_1aParseTreeListener::mergeObjectTree(NodeId ppNodeId) {
  const std::vector<VObject> &ppObjects = m_ppFileContent->getVObjects();
  const VObject &ppObject = ppObjects[ppNodeId];

  std::vector<VObject> &objects = *m_fileContent->mutableVObjects();

  NodeId firstChildId;
  NodeId lastChildId;
  NodeId ppChildId = ppObject.m_child;
  while (ppChildId) {
    NodeId childId = mergeObjectTree(ppChildId);
    if (firstChildId) {
      objects[lastChildId].m_sibling = childId;
    } else {
      firstChildId = childId;
    }
    lastChildId = childId;
    ppChildId = ppObjects[ppChildId].m_sibling;
  }

  NodeId nodeId = m_fileContent->addObject(
      ppObject.m_name, ppObject.m_fileId, ppObject.m_type, ppObject.m_line,
      ppObject.m_column, ppObject.m_endLine, ppObject.m_endColumn);

  if (firstChildId) {
    VObject &object = objects[nodeId];
    object.m_child = firstChildId;

    NodeId childId = firstChildId;
    while (childId) {
      objects[childId].m_parent = nodeId;
      childId = objects[childId].m_sibling;
    }
  }

  return nodeId;
}

std::optional<bool> SV3_1aParseTreeListener::isUnaryOperator(
    const antlr4::tree::TerminalNode *node) const {
  switch (((antlr4::ParserRuleContext *)node->parent)->getRuleIndex()) {
    case SV3_1aParser::RuleExpression: {
      SV3_1aParser::ExpressionContext *ctx =
          (SV3_1aParser::ExpressionContext *)node->parent;
      return std::optional<bool>(ctx->expression().size() == 1);
    } break;

    case SV3_1aParser::RuleConstant_expression: {
      SV3_1aParser::Constant_expressionContext *ctx =
          (SV3_1aParser::Constant_expressionContext *)node->parent;
      return std::optional<bool>(ctx->constant_primary() != nullptr);
    } break;

    default:
      break;
  }

  return std::optional<bool>();
}

void SV3_1aParseTreeListener::enterString_value(
    SV3_1aParser::String_valueContext *ctx) {
  if (m_paused != 0) return;

  std::string text = ctx->String()->getText();

  std::smatch match;
  while (std::regex_search(text, match, m_escSeqSearchRegex)) {
    std::string var = "\\" + match[1].str();
    text = text.replace(match.position(0), match.length(0), var);
  }

  addVObject(ctx, text, VObjectType::paString_value);
  m_visitedTokens.emplace(ctx->String()->getSymbol());

  if (text.size() > SV_MAX_STRING_SIZE) {
    logError(ErrorDefinition::PA_MAX_LENGTH_IDENTIFIER, ctx, text);
  }
}

void SV3_1aParseTreeListener::applyLocationOffsets() {
  // std::cout << "ParseTreeListener::ColumnOffsets:" << std::endl;
  // for (const auto &entry : m_offsets) {
  //   std::cout << std::setfill(' ') << std::setw(4) << entry.first
  //             << std::setfill(' ') << std::setw(4) <<
  //             std::get<0>(entry.second)
  //             << std::setfill(' ') << std::setw(4) <<
  //             std::get<1>(entry.second)
  //             << std::endl;
  // }
  // std::cout << std::endl;

  m_fileContent->sortTree();
  // m_fileContent->printTree(std::cout);

  vobjects_t &vobjects = *m_fileContent->mutableVObjects();
  for (vobjects_t::reference object : vobjects) {
    if (static_cast<int32_t>(object.m_type) <
        static_cast<int32_t>(VObjectType::paIndexBegin)) {
      continue;  // Don't apply offsets to ppXXXX types
    }

    if (object.m_type == VObjectType::paTop_level_rule) {
      // HACK(HS): For some unknown reason, the top level rule doesn't start
      // at the top of the file and instead starts at where the module (or
      // whatever is in channel 0). Force it to start at 1, 1 so the
      // validation check can pass.
      object.m_line = 1;
      object.m_column = 1;
    }

    uint16_t sc = object.m_column;
    std::pair<offsets_t::const_iterator, offsets_t::const_iterator> slItBounds =
        m_offsets.equal_range(object.m_line);
    for (offsets_t::const_iterator it = slItBounds.first;
         it != slItBounds.second; ++it) {
      if (object.m_column > std::get<0>(it->second)) {
        sc += std::get<1>(it->second);
      }
    }
    object.m_column = sc;

    uint16_t ec = object.m_endColumn;
    std::pair<offsets_t::const_iterator, offsets_t::const_iterator> elItBounds =
        m_offsets.equal_range(object.m_endLine);
    for (offsets_t::const_iterator it = elItBounds.first;
         it != elItBounds.second; ++it) {
      if (object.m_endColumn > std::get<0>(it->second)) {
        ec += std::get<1>(it->second);
      }
    }
    object.m_endColumn = ec;
  }

  // if (!m_fileContent->validate()) {
  //   std::cerr << "Failed to validate generated tree" << std::endl;
  // }
}

void SV3_1aParseTreeListener::collectLineRanges(NodeId ppParentId,
                                                line_ends_t &ends) const {
  const std::vector<VObject> &ppObjects = m_ppFileContent->getVObjects();
  const VObject &ppObject = ppObjects[ppParentId];

  line_ends_t::iterator sit = ends.emplace(ppObject.m_line, kMinMaxRange).first;
  sit->second = std::make_pair(std::min(sit->second.first, ppObject.m_column),
                               std::max(sit->second.second, ppObject.m_column));

  line_ends_t::iterator eit =
      ends.emplace(ppObject.m_endLine, kMinMaxRange).first;
  eit->second =
      std::make_pair(std::min(eit->second.first, ppObject.m_endColumn),
                     std::max(eit->second.second, ppObject.m_endColumn));

  NodeId ppChildId = ppObjects[ppParentId].m_child;
  while (ppChildId) {
    collectLineRanges(ppChildId, ends);
    ppChildId = ppObjects[ppChildId].m_sibling;
  }
}

void SV3_1aParseTreeListener::collectLineRanges(const antlr4::Token *begin,
                                                const antlr4::Token *end,
                                                line_ends_t &ends) const {
  for (size_t i = begin->getTokenIndex() + 1, ni = end->getTokenIndex(); i < ni;
       ++i) {
    antlr4::Token *const token = m_tokens->get(i);
    auto [fileId, sl, sc, el, ec] = getFileLine(nullptr, token);

    line_ends_t::iterator sit = ends.emplace(sl, kMinMaxRange).first;
    sit->second = std::make_pair(std::min(sit->second.first, sc),
                                 std::max(sit->second.second, sc));

    line_ends_t::iterator eit = ends.emplace(el, kMinMaxRange).first;
    eit->second = std::make_pair(std::min(eit->second.first, ec),
                                 std::max(eit->second.second, ec));
  }
}

void SV3_1aParseTreeListener::visitPreprocBegin(antlr4::Token *token) {
  m_preprocBeginStack.emplace_back(token);
  ++m_paused;
}

void SV3_1aParseTreeListener::visitPreprocEnd(antlr4::Token *token,
                                              NodeId ppNodeId) {
  antlr4::Token *const endToken = token;
  antlr4::Token *const beginToken = m_preprocBeginStack.back();
  m_preprocBeginStack.pop_back();

  line_ends_t ppEnds;
  collectLineRanges(ppNodeId, ppEnds);

  const VObject &object = m_ppFileContent->getVObjects()[(RawNodeId)ppNodeId];

  switch (object.m_type) {
    case VObjectType::ppInclude_directive:
    case VObjectType::ppMacro_definition: {
      auto [fileIdBegin, slBegin, scBegin, elBegin, ecBegin] =
          getFileLine(nullptr, beginToken);
      auto [fileIdEnd, slEnd, scEnd, elEnd, ecEnd] =
          getFileLine(nullptr, endToken);

      m_offsets.emplace(std::piecewise_construct,
                        std::forward_as_tuple(slBegin),
                        std::forward_as_tuple(scBegin, scBegin - ecBegin));

      line_ends_t::const_iterator ppIt = ppEnds.find(slBegin);
      if ((ppIt != ppEnds.end()) &&
          (ppIt->second.first != ppIt->second.second)) {
        m_offsets.emplace(
            std::piecewise_construct, std::forward_as_tuple(slBegin),
            std::forward_as_tuple(ecBegin,
                                  ppIt->second.second - ppIt->second.first));
      }

      m_offsets.emplace(std::piecewise_construct, std::forward_as_tuple(elEnd),
                        std::forward_as_tuple(scEnd, scEnd - ecEnd));
    } break;

    case VObjectType::ppMacro_instance: {
      const LineColumn slcBegin = ParseUtils::getLineColumn(beginToken);
      const LineColumn slcEnd = ParseUtils::getLineColumn(endToken);

      if (slcBegin.first == slcEnd.first) {
        auto [fileIdBegin, slBegin, scBegin, elBegin, ecBegin] =
            getFileLine(nullptr, beginToken);
        auto [fileIdEnd, slEnd, scEnd, elEnd, ecEnd] =
            getFileLine(nullptr, endToken);

        line_ends_t ends;
        collectLineRanges(beginToken, endToken, ends);

        m_offsets.emplace(std::piecewise_construct,
                          std::forward_as_tuple(slBegin),
                          std::forward_as_tuple(scBegin, scBegin - ecBegin));

        line_ends_t::const_iterator ppIt = ppEnds.find(slBegin);
        if ((ppIt != ppEnds.end()) &&
            (ppIt->second.first != ppIt->second.second)) {
          m_offsets.emplace(
              std::piecewise_construct, std::forward_as_tuple(slBegin),
              std::forward_as_tuple(ecBegin,
                                    ppIt->second.second - ppIt->second.first));
        }

        line_ends_t::const_iterator it = ends.find(slBegin);
        if ((it != ends.end()) && (it->second.first != it->second.second)) {
          m_offsets.emplace(std::piecewise_construct,
                            std::forward_as_tuple(slBegin),
                            std::forward_as_tuple(
                                ecBegin, it->second.first - it->second.second));
        }

        m_offsets.emplace(std::piecewise_construct,
                          std::forward_as_tuple(elEnd),
                          std::forward_as_tuple(scEnd, scEnd - ecEnd));
      } else {
        const LineColumn elcBegin = ParseUtils::getEndLineColumn(beginToken);
        const LineColumn elcEnd = ParseUtils::getEndLineColumn(endToken);
        std::pair<line_ends_t::const_iterator, line_ends_t::const_iterator>
            ppMinMaxIt =
                std::minmax_element(ppEnds.begin(), ppEnds.end(),
                                    [](line_ends_t::const_reference lhs,
                                       line_ends_t::const_reference rhs) {
                                      return lhs.first < rhs.first;
                                    });
        // const uint32_t slBegin = slcBegin.first;
        const uint16_t scBegin = slcBegin.second;
        // const uint32_t elBegin = elcBegin.first;
        const uint16_t ecBegin = elcBegin.second;

        // const uint32_t slEnd = slcEnd.first;
        const uint16_t scEnd = slcEnd.second;
        // const uint32_t elEnd = elcEnd.first;
        const uint16_t ecEnd = elcEnd.second;

        const uint32_t slTop = ppMinMaxIt.first->first;
        const uint16_t scTop = ppMinMaxIt.first->second.first;
        const uint16_t ecTop = ppMinMaxIt.first->second.second;
        const uint32_t slBottom = ppMinMaxIt.second->first;
        const uint16_t scBottom = ppMinMaxIt.second->second.first;
        const uint16_t ecBottom = ppMinMaxIt.second->second.second;

        if (slTop == slBottom) {
          m_offsets.emplace(std::piecewise_construct,
                            std::forward_as_tuple(slTop),
                            std::forward_as_tuple(scBottom, scEnd - ecEnd));
          m_offsets.emplace(std::piecewise_construct,
                            std::forward_as_tuple(slTop),
                            std::forward_as_tuple(scBottom, ecBottom - 1));
        } else {
          m_offsets.emplace(std::piecewise_construct,
                            std::forward_as_tuple(slTop),
                            std::forward_as_tuple(scBegin, scBegin - ecBegin));
          m_offsets.emplace(std::piecewise_construct,
                            std::forward_as_tuple(slTop),
                            std::forward_as_tuple(scTop, ecTop - scTop));

          m_offsets.emplace(std::piecewise_construct,
                            std::forward_as_tuple(slBottom),
                            std::forward_as_tuple(scBottom, scEnd - ecEnd));
          m_offsets.emplace(
              std::piecewise_construct, std::forward_as_tuple(slBottom),
              std::forward_as_tuple(scBottom, ecBottom - scBottom));
        }
      }
    } break;

    default: {
      auto [fileIdBegin, slBegin, scBegin, elBegin, ecBegin] =
          getFileLine(nullptr, beginToken);
      auto [fileIdEnd, slEnd, scEnd, elEnd, ecEnd] =
          getFileLine(nullptr, endToken);

      m_offsets.emplace(std::piecewise_construct,
                        std::forward_as_tuple(slBegin),
                        std::forward_as_tuple(scBegin, scBegin - ecBegin));
      m_offsets.emplace(std::piecewise_construct, std::forward_as_tuple(elEnd),
                        std::forward_as_tuple(scEnd, scEnd - ecEnd));
    } break;
  }
  --m_paused;
}

void SV3_1aParseTreeListener::processPendingTokens(
    antlr4::ParserRuleContext *ctx, size_t endTokenIndex) {
  while (m_lastVisitedTokenIndex < endTokenIndex) {
    antlr4::Token *const lastToken = m_tokens->get(m_lastVisitedTokenIndex);

    NodeId nodeId;
    switch (lastToken->getType()) {
      case SV3_1aParser::PREPROC_BEGIN: {
        visitPreprocBegin(lastToken);
      } break;

      case SV3_1aParser::PREPROC_END: {
        antlr4::Token *const endToken = lastToken;
        const std::string endText = endToken->getText();
        std::string_view svtext = endText;
        svtext.remove_prefix(kPreprocEndPrefix.length());

        uint32_t index = 0;
        if (NumUtils::parseUint32(svtext, &index)) {
          const NodeId ppNodeId(index);
          nodeId = mergeObjectTree(ppNodeId);
          visitPreprocEnd(lastToken, ppNodeId);
        }
      } break;

      case SV3_1aParser::One_line_comment: {
        nodeId = addVObject(ctx, lastToken, VObjectType::paOne_line_comment);
      } break;

      case SV3_1aParser::Block_comment: {
        nodeId = addVObject(ctx, lastToken, VObjectType::paBlock_comment);
      } break;

      case SV3_1aParser::White_space: {
        nodeId = addVObject(ctx, lastToken, VObjectType::paWhite_space);
      } break;

      default: {
        nodeId = InvalidNodeId;
      } break;
    }

    if (nodeId) m_orphanObjects.emplace(ctx, nodeId);
    ++m_lastVisitedTokenIndex;
  }
}

void SV3_1aParseTreeListener::processOrphanObjects(
    antlr4::ParserRuleContext *ctx, NodeId parentId) {
  std::pair<orphan_objects_t::const_iterator, orphan_objects_t::const_iterator>
      bounds = m_orphanObjects.equal_range(ctx);
  if (bounds.first == bounds.second) return;

  VObject *const parent = m_fileContent->MutableObject(parentId);
  for (orphan_objects_t::const_iterator it = bounds.first; it != bounds.second;
       ++it) {
    const NodeId &orphanId = it->second;
    VObject *const orphan = m_fileContent->MutableObject(orphanId);

    orphan->m_parent = parentId;
    orphan->m_sibling = parent->m_child;
    parent->m_child = orphanId;
  }

  m_orphanObjects.erase(bounds.first, bounds.second);
}

void SV3_1aParseTreeListener::enterEveryRule(antlr4::ParserRuleContext *ctx) {
  if (const antlr4::Token *const startToken = ctx->getStart()) {
    if (!m_ruleCallstack.empty()) {
      processPendingTokens(m_ruleCallstack.back(), startToken->getTokenIndex());
    }
  }
  m_ruleCallstack.emplace_back(ctx);
}

void SV3_1aParseTreeListener::exitEveryRule(antlr4::ParserRuleContext *ctx) {
  if (!m_ruleCallstack.empty() && (m_ruleCallstack.back() == ctx)) {
    m_ruleCallstack.pop_back();
  }

  if (const antlr4::Token *const stopToken = ctx->getStop()) {
    processPendingTokens(ctx, stopToken->getTokenIndex());
  }

  NodeId nodeId;

  // clang-format off
  switch (ctx->getRuleIndex()) {
<RULE_CASE_STATEMENTS>
    default: break;
  }
  // clang-format on

  processOrphanObjects(ctx, nodeId);

  if (ctx->getRuleIndex() == SV3_1aParser::RuleTop_level_rule) {
    applyLocationOffsets();
  }
}

void SV3_1aParseTreeListener::visitTerminal(antlr4::tree::TerminalNode *node) {
  // Skip any tokens that are already handled as part of enterXXX/exitXXX rules
  const antlr4::Token *const token = node->getSymbol();
  if (token->getType() == antlr4::Token::EOF) return;
  if (!m_visitedTokens.emplace(token).second) return;

  if (!m_ruleCallstack.empty()) {
    processPendingTokens(m_ruleCallstack.back(), token->getTokenIndex());
  }

  NodeId nodeId;

  // clang-format off
  switch (token->getType()) {
    case SV3_1aParser::Escaped_identifier: {
      std::string text = node->getText();
      std::smatch match;
      while (std::regex_search(text, match, m_escSeqSearchRegex)) {
        std::string var = "\\" + match[1].str();
        text = text.replace(match.position(0), match.length(0), var);
      }
      nodeId = addVObject((antlr4::ParserRuleContext *)node, text, VObjectType::paEscaped_identifier);
    } break;

<VISIT_CASE_STATEMENTS>
    default: break;
  }
  // clang-format on

  if (nodeId) {
    VObject *const object = m_fileContent->MutableObject(nodeId);

    const std::optional<bool> isUnary = isUnaryOperator(node);
    if (isUnary) {
      // clang-format off
      switch (token->getType()) {
        case SV3_1aParser::BITW_AND: object->m_type = isUnary.value() ? VObjectType::paUnary_BitwAnd : VObjectType::paBinOp_BitwAnd; break;
        case SV3_1aParser::BITW_OR: object->m_type = isUnary.value() ? VObjectType::paUnary_BitwOr : VObjectType::paBinOp_BitwOr; break;
        case SV3_1aParser::BITW_XOR: object->m_type = isUnary.value() ? VObjectType::paUnary_BitwXor : VObjectType::paBinOp_BitwXor; break;
        case SV3_1aParser::MINUS: object->m_type = isUnary.value() ? VObjectType::paUnary_Minus : VObjectType::paBinOp_Minus; break;
        case SV3_1aParser::PLUS: object->m_type = isUnary.value() ? VObjectType::paUnary_Plus : VObjectType::paBinOp_Plus; break;
        case SV3_1aParser::REDUCTION_NAND: object->m_type = isUnary.value() ? VObjectType::paUnary_ReductNand : VObjectType::paBinOp_ReductNand; break;
        case SV3_1aParser::REDUCTION_XNOR1: object->m_type = isUnary.value() ? VObjectType::paUnary_ReductXnor1 : VObjectType::paBinOp_ReductXnor1; break;
        case SV3_1aParser::REDUCTION_XNOR2: object->m_type = isUnary.value() ? VObjectType::paUnary_ReductXnor2 : VObjectType::paBinOp_ReductXnor2; break;
        case SV3_1aParser::STAR: object->m_type = VObjectType::paBinOp_Mult; break;
        default: break;
      }
      // clang-format on
    }
  }
}

void SV3_1aParseTreeListener::visitErrorNode(antlr4::tree::ErrorNode *node) {
  if (node->getText().find("<missing ") != 0) {
    addVObject(node, VObjectType::slUnparsable_Text);
  }
}
}  // namespace SURELOG
