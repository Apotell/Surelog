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
 * File:   SV3_1aPpParseTreeListener.cpp
 * Author: hs
 *
 * Created on January 31, 2023, 12:00 PM
 */

#include <Surelog/CommandLine/CommandLineParser.h>
#include <Surelog/Common/FileSystem.h>
#include <Surelog/Common/Session.h>
#include <Surelog/Design/FileContent.h>
#include <Surelog/SourceCompile/CompileSourceFile.h>
#include <Surelog/SourceCompile/MacroInfo.h>
#include <Surelog/SourceCompile/PreprocessFile.h>
#include <Surelog/SourceCompile/SV3_1aPpParseTreeListener.h>
#include <Surelog/SourceCompile/SymbolTable.h>
#include <Surelog/Utils/ParseUtils.h>
#include <Surelog/Utils/StringUtils.h>
#include <parser/SV3_1aPpParser.h>

namespace SURELOG {
SV3_1aPpParseTreeListener::SV3_1aPpParseTreeListener(
    Session *session, PreprocessFile *pp, antlr4::CommonTokenStream *tokens,
    PreprocessFile::SpecialInstructions &instructions)
    : SV3_1aPpTreeListenerHelper(session, pp, instructions, tokens) {
  if (m_pp->getFileContent() == nullptr) {
    m_fileContent = new FileContent(session, m_pp->getFileId(0),
                                    m_pp->getLibrary(), nullptr, BadPathId);
    m_pp->setFileContent(m_fileContent);
  } else {
    m_fileContent = m_pp->getFileContent();
  }

  if (PreprocessFile *includer = pp->getIncluder()) {
    m_paused = ((SV3_1aPpParseTreeListener *)includer->m_listener)->m_paused;
  }
}

NodeId SV3_1aPpParseTreeListener::addVObject(antlr4::tree::TerminalNode *node,
                                             VObjectType objectType) {
  return addVObject((antlr4::ParserRuleContext *)node, node->getText(),
                    objectType);
}

bool SV3_1aPpParseTreeListener::isOnCallStack(size_t ruleIndex) const {
  return std::find(m_callstack.crbegin(), m_callstack.crend(), ruleIndex) !=
         m_callstack.crend();
}

bool SV3_1aPpParseTreeListener::isAnyOnCallStack(
    const std::unordered_set<size_t> &ruleIndicies) const {
  for (callstack_t::const_reverse_iterator it = m_callstack.crbegin(),
                                           end = m_callstack.crend();
       it != end; ++it) {
    if (ruleIndicies.find(*it) != ruleIndicies.end()) {
      return true;
    }
  }
  return false;
}

void SV3_1aPpParseTreeListener::appendPreprocBegin() {
  const size_t index = m_fileContent->getVObjects().size();
  m_pp->append(StrCat(kPreprocBeginPrefix, index, kPreprocBeginSuffix,
                      std::string(m_pendingCRs, '\n')));
  m_pendingCRs = 0;
}

void SV3_1aPpParseTreeListener::appendPreprocEnd(antlr4::ParserRuleContext *ctx,
                                                 VObjectType type) {
  const size_t index = (RawNodeId)addVObject(ctx, type);
  m_pp->append(StrCat(std::string(m_pendingCRs, '\n'), kPreprocEndPrefix, index,
                      kPreprocEndSuffix));
  m_visitedRules.emplace(ctx);
  m_pendingCRs = 0;
}

void SV3_1aPpParseTreeListener::enterText_blob(
    SV3_1aPpParser::Text_blobContext *ctx) {
  if (m_paused != 0) return;

  if (m_inActiveBranch && isOnCallStack(SV3_1aPpParser::RuleMacro_instance)) {
    m_pp->append(ctx->getText());
  }
}

void SV3_1aPpParseTreeListener::enterEscaped_identifier(
    SV3_1aPpParser::Escaped_identifierContext *ctx) {
  if (m_paused != 0) return;

  if (m_inActiveBranch && !m_inMacroDefinitionParsing) {
    const std::string text = ctx->getText();

    std::string sequence = kEscapeSequence;
    sequence.append(++text.cbegin(), text.cend());
    sequence.append(kEscapeSequence);

    m_pp->append(sequence);
  }
}

void SV3_1aPpParseTreeListener::enterIfdef_directive(
    SV3_1aPpParser::Ifdef_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocBegin();

  std::string macroName;
  LineColumn lc = ParseUtils::getLineColumn(m_pp->getTokenStream(), ctx);
  if (antlr4::tree::TerminalNode *const simpleIdentifierNode =
          ctx->Simple_identifier()) {
    lc = ParseUtils::getLineColumn(simpleIdentifierNode);
    macroName = simpleIdentifierNode->getText();
  } else if (antlr4::tree::TerminalNode *const escapedIdentifierNode =
                 ctx->ESCAPED_IDENTIFIER()) {
    lc = ParseUtils::getLineColumn(escapedIdentifierNode);
    macroName = escapedIdentifierNode->getText();
    std::string_view svname = macroName;
    svname.remove_prefix(1);
    macroName = StringUtils::rtrim(svname);
  } else if (SV3_1aPpParser::Macro_instanceContext *const macroInstanceNode =
                 ctx->macro_instance()) {
    lc = ParseUtils::getLineColumn(m_pp->getTokenStream(), macroInstanceNode);
    macroName = m_pp->evaluateMacroInstance(
        macroInstanceNode->getText(), m_pp, lc.first,
        PreprocessFile::SpecialInstructions::CheckLoop,
        PreprocessFile::SpecialInstructions::ComplainUndefinedMacro);
  }

  if (!m_pp->isMacroBody()) {
    m_pp->getSourceFile()->m_loopChecker.clear();
  }

  std::vector<std::string> args;
  PreprocessFile::SpecialInstructions instr = m_pp->m_instructions;
  instr.m_evaluate = PreprocessFile::SpecialInstructions::DontEvaluate;
  const auto [result, macroBody, tokenPositions] = m_pp->getMacro(
      macroName, args, m_pp, 0, m_pp->getSourceFile()->m_loopChecker, instr);

  PreprocessFile::IfElseItem &item = m_pp->getStack().emplace_back();
  item.m_macroName = macroName;
  item.m_defined = (macroBody != PreprocessFile::MacroNotDefined);
  item.m_type = PreprocessFile::IfElseItem::IFDEF;
  item.m_previousActiveState = m_inActiveBranch;
  setCurrentBranchActivity(lc.first);
}

void SV3_1aPpParseTreeListener::exitIfdef_directive(
    SV3_1aPpParser::Ifdef_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppIfdef_directive);
}

void SV3_1aPpParseTreeListener::enterIfndef_directive(
    SV3_1aPpParser::Ifndef_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocBegin();
}

void SV3_1aPpParseTreeListener::exitIfndef_directive(
    SV3_1aPpParser::Ifndef_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppIfndef_directive);
}

void SV3_1aPpParseTreeListener::enterUndef_directive(
    SV3_1aPpParser::Undef_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocBegin();
}

void SV3_1aPpParseTreeListener::exitUndef_directive(
    SV3_1aPpParser::Undef_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppUndef_directive);
}

void SV3_1aPpParseTreeListener::enterElsif_directive(
    SV3_1aPpParser::Elsif_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocBegin();

  std::string macroName;
  LineColumn lc = ParseUtils::getLineColumn(m_pp->getTokenStream(), ctx);
  if (antlr4::tree::TerminalNode *const simpleIdentifierNode =
          ctx->Simple_identifier()) {
    lc = ParseUtils::getLineColumn(simpleIdentifierNode);
    macroName = simpleIdentifierNode->getText();
  } else if (antlr4::tree::TerminalNode *const escapedIdentifierNode =
                 ctx->ESCAPED_IDENTIFIER()) {
    lc = ParseUtils::getLineColumn(escapedIdentifierNode);
    macroName = escapedIdentifierNode->getText();
    std::string_view svname = macroName;
    svname.remove_prefix(1);
    macroName = StringUtils::rtrim(svname);
  } else if (SV3_1aPpParser::Macro_instanceContext *const macroInstanceNode =
                 ctx->macro_instance()) {
    lc = ParseUtils::getLineColumn(m_pp->getTokenStream(), macroInstanceNode);
    macroName = m_pp->evaluateMacroInstance(
        macroInstanceNode->getText(), m_pp, lc.first,
        PreprocessFile::SpecialInstructions::CheckLoop,
        PreprocessFile::SpecialInstructions::ComplainUndefinedMacro);
  }

  if (!m_pp->isMacroBody()) {
    m_pp->getSourceFile()->m_loopChecker.clear();
  }

  std::vector<std::string> args;
  PreprocessFile::SpecialInstructions instr = m_pp->m_instructions;
  instr.m_evaluate = PreprocessFile::SpecialInstructions::DontEvaluate;
  const auto [result, macroBody, tokenPositions] = m_pp->getMacro(
      macroName, args, m_pp, 0, m_pp->getSourceFile()->m_loopChecker, instr);

  const bool previousBranchActive = isPreviousBranchActive();
  PreprocessFile::IfElseItem &item = m_pp->getStack().emplace_back();
  item.m_macroName = macroName;
  item.m_defined =
      (macroBody != PreprocessFile::MacroNotDefined) && !previousBranchActive;
  item.m_type = PreprocessFile::IfElseItem::ELSIF;
  setCurrentBranchActivity(lc.first);
}

void SV3_1aPpParseTreeListener::exitElsif_directive(
    SV3_1aPpParser::Elsif_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppElsif_directive);
}

void SV3_1aPpParseTreeListener::enterElse_directive(
    SV3_1aPpParser::Else_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocBegin();

  const bool previousBranchActive = isPreviousBranchActive();
  PreprocessFile::IfElseItem &item = m_pp->getStack().emplace_back();
  LineColumn lc = ParseUtils::getLineColumn(m_pp->getTokenStream(), ctx);
  item.m_defined = !previousBranchActive;
  item.m_type = PreprocessFile::IfElseItem::ELSE;
  setCurrentBranchActivity(lc.first);
}

void SV3_1aPpParseTreeListener::exitElse_directive(
    SV3_1aPpParser::Else_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppElse_directive);
}

void SV3_1aPpParseTreeListener::enterElseif_directive(
    SV3_1aPpParser::Elseif_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocBegin();
}

void SV3_1aPpParseTreeListener::exitElseif_directive(
    SV3_1aPpParser::Elseif_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppElseif_directive);
}

void SV3_1aPpParseTreeListener::enterEndif_directive(
    SV3_1aPpParser::Endif_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocBegin();

  PreprocessFile::IfElseStack &stack = m_pp->getStack();
  LineColumn lc = ParseUtils::getLineColumn(m_pp->getTokenStream(), ctx);
  if (!stack.empty()) {
    bool unroll = true;
    // if (ctx->One_line_comment()) {
    //   addLineFiller(ctx);
    // }
    while (unroll && (!stack.empty())) {
      PreprocessFile::IfElseItem &item = stack.back();
      switch (item.m_type) {
        case PreprocessFile::IfElseItem::IFDEF:
        case PreprocessFile::IfElseItem::IFNDEF:
          m_inActiveBranch = item.m_previousActiveState;
          stack.pop_back();
          unroll = false;
          break;
        case PreprocessFile::IfElseItem::ELSIF:
        case PreprocessFile::IfElseItem::ELSE:
          stack.pop_back();
          break;
        default:
          unroll = false;
          break;
      }
    }
  }
  setCurrentBranchActivity(lc.first);
}

void SV3_1aPpParseTreeListener::exitEndif_directive(
    SV3_1aPpParser::Endif_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppEndif_directive);
}

void SV3_1aPpParseTreeListener::enterInclude_directive(
    SV3_1aPpParser::Include_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocBegin();

  if (!(m_inActiveBranch && (!m_inMacroDefinitionParsing))) {
    return;
  }

  FileSystem *const fileSystem = m_session->getFileSystem();

  LineColumn slc = ParseUtils::getLineColumn(m_pp->getTokenStream(), ctx);
  LineColumn elc = ParseUtils::getEndLineColumn(m_pp->getTokenStream(), ctx);

  std::string fileName;
  if (antlr4::tree::TerminalNode *const stringNode = ctx->STRING()) {
    fileName = stringNode->getText();
    slc = ParseUtils::getLineColumn(stringNode);
    elc = ParseUtils::getEndLineColumn(stringNode);
  } else if (SV3_1aPpParser::Macro_instanceContext *const macroInstanceNode =
                 ctx->macro_instance()) {
    slc = ParseUtils::getLineColumn(m_pp->getTokenStream(), macroInstanceNode);
    elc =
        ParseUtils::getEndLineColumn(m_pp->getTokenStream(), macroInstanceNode);
    fileName = m_pp->evaluateMacroInstance(
        macroInstanceNode->getText(), m_pp, slc.first,
        PreprocessFile::SpecialInstructions::CheckLoop,
        PreprocessFile::SpecialInstructions::ComplainUndefinedMacro);
  } else {
    Location loc(m_pp->getFileId(slc.first), m_pp->getLineNb(slc.first),
                 slc.second);
    logError(ErrorDefinition::PP_INVALID_INCLUDE_FILENAME, loc);
    return;
  }

  fileName = StringUtils::unquoted(StringUtils::trim(fileName));

  SymbolTable *const symbols = m_session->getSymbolTable();

  PathId fileId = fileSystem->locate(
      fileName, m_session->getCommandLineParser()->getIncludePaths(), symbols);
  if (!fileId) {
    // If failed to locate, then assume the same folder as the includer file
    // and let it fail down the stream.
    fileId = fileSystem->getSibling(m_pp->getCompileSourceFile()->getFileId(),
                                    fileName, symbols);
  }

  if (m_session->getCommandLineParser()->verbose()) {
    Location loc(fileId);
    logError(ErrorDefinition::PP_PROCESSING_INCLUDE_FILE, loc, true);
  }

  // Detect include loop
  PreprocessFile *tmp = m_pp;
  while (tmp) {
    if (tmp->getFileId(0) == fileId) {
      Location loc(m_pp->getFileId(slc.first), slc.first, slc.second,
                   (SymbolId)fileId);
      logError(ErrorDefinition::PP_RECURSIVE_INCLUDE_DIRECTIVE, loc, true);
      return;
    }
    tmp = tmp->getIncluder();
  }

  PreprocessFile *pp = new PreprocessFile(
      m_session, fileId, m_pp->getCompileSourceFile(), m_instructions,
      m_pp->getCompilationUnit(), m_pp->getLibrary(), m_pp, slc.first);
  m_pp->getCompileSourceFile()->registerPP(pp);
  if (!pp->preprocess()) {
    return;
  }

  if (ctx->macro_instance()) {
    m_append_paused_context = ctx;
    m_pp->pauseAppend();
  }
}

void SV3_1aPpParseTreeListener::exitInclude_directive(
    SV3_1aPpParser::Include_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppInclude_directive);

  if (m_append_paused_context == ctx) {
    m_append_paused_context = nullptr;
    m_pp->resumeAppend();
  }

  if (antlr4::tree::TerminalNode *const escapedIdentifier =
          ctx->ESCAPED_IDENTIFIER()) {
    addVObject(escapedIdentifier, VObjectType::ppEscaped_identifier);
  } else if (antlr4::tree::TerminalNode *const simpleIdentifier =
                 ctx->Simple_identifier()) {
    addVObject(simpleIdentifier, VObjectType::ppPs_identifier);
  } else if (antlr4::tree::TerminalNode *const stringNode = ctx->STRING()) {
    addVObject(stringNode, VObjectType::ppString);
  }
  addVObject(ctx, VObjectType::ppInclude_directive);
}

void SV3_1aPpParseTreeListener::enterLine_directive(
    SV3_1aPpParser::Line_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocBegin();
}

void SV3_1aPpParseTreeListener::exitLine_directive(
    SV3_1aPpParser::Line_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppLine_directive);
}

void SV3_1aPpParseTreeListener::enterSv_file_directive(
    SV3_1aPpParser::Sv_file_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocBegin();
}

void SV3_1aPpParseTreeListener::exitSv_file_directive(
    SV3_1aPpParser::Sv_file_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppSv_file_directive);
}

void SV3_1aPpParseTreeListener::enterSv_line_directive(
    SV3_1aPpParser::Sv_line_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocBegin();
}

void SV3_1aPpParseTreeListener::exitSv_line_directive(
    SV3_1aPpParser::Sv_line_directiveContext *ctx) {
  if (m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppSv_line_directive);
}

void SV3_1aPpParseTreeListener::enterMacroInstanceWithArgs(
    SV3_1aPpParser::MacroInstanceWithArgsContext *ctx) {
  if (m_paused++ == 0) appendPreprocBegin();
  if (m_inMacroDefinitionParsing) return;

  if (m_inActiveBranch) {
    LineColumn slc = ParseUtils::getLineColumn(m_pp->getTokenStream(), ctx);
    LineColumn elc = ParseUtils::getEndLineColumn(m_pp->getTokenStream(), ctx);

    std::string macroName;
    if (antlr4::tree::TerminalNode *const identifier =
            ctx->Macro_identifier()) {
      macroName = identifier->getText();
      slc = ParseUtils::getLineColumn(identifier);
      elc = ParseUtils::getEndLineColumn(identifier);
    } else if (antlr4::tree::TerminalNode *escapedIdentifier =
                   ctx->Macro_Escaped_identifier()) {
      macroName = escapedIdentifier->getText();
      std::string_view svname = macroName;
      svname.remove_prefix(1);
      macroName = StringUtils::rtrim(svname);
      slc = ParseUtils::getLineColumn(escapedIdentifier);
      elc = ParseUtils::getEndLineColumn(escapedIdentifier);
    }

    std::vector<antlr4::tree::ParseTree *> tokens =
        ParseUtils::getTopTokenList(ctx->macro_actual_args());
    std::vector<std::string> actualArgs;
    ParseUtils::tokenizeAtComma(actualArgs, tokens);
    macroName.erase(macroName.begin());

    int32_t openingIndex = -1;
    if (!m_pp->isMacroBody()) {
      m_pp->getSourceFile()->m_loopChecker.clear();
    }

    std::tuple<bool, std::string, std::vector<LineColumn>> evalResult;
    MacroInfo *const macroInfo = m_pp->getMacro(macroName);
    if (macroInfo != nullptr) {
      if (m_pp == m_pp->getSourceFile()) {
        PathId fileId = m_pp->getRawFileId();
        if (!fileId && m_pp->getEmbeddedMacroCallFile()) {
          fileId = m_pp->getEmbeddedMacroCallFile();
          slc.first += m_pp->getEmbeddedMacroCallLine() - 1;
          elc.first += m_pp->getEmbeddedMacroCallLine() - 1;
        }

        const LineColumn sourceLineColumnStart = m_pp->getCurrentPosition();
        openingIndex = m_pp->getSourceFile()->addIncludeFileInfo(
            /* context */ IncludeFileInfo::Context::MACRO,
            /* action */ IncludeFileInfo::Action::PUSH,
            /* macroDefinition */ macroInfo,
            /* sectionFileId */ fileId,
            /* sectionLine */ macroInfo->m_startLine,
            /* sectionColumn */ macroInfo->m_bodyStartColumn,
            /* sourceLine */ sourceLineColumnStart.first,
            /* sourceColumn */ sourceLineColumnStart.second,
            /* sectionSymbolId */ BadSymbolId,
            /* symbolLine */ slc.first,
            /* symbolColumn */ slc.second);
      }
      evalResult = m_pp->getMacro(macroName, actualArgs, m_pp, slc.first,
                                  m_pp->getSourceFile()->m_loopChecker,
                                  m_pp->m_instructions, macroInfo->m_fileId,
                                  macroInfo->m_startLine);
    } else {
      evalResult = m_pp->getMacro(macroName, actualArgs, m_pp, slc.first,
                                  m_pp->getSourceFile()->m_loopChecker,
                                  m_pp->m_instructions);
    }

    const std::string macroArgs = ctx->macro_actual_args()->getText();
    int32_t nbCRinArgs = std::count(macroArgs.begin(), macroArgs.end(), '\n');

    std::string &macroBody = std::get<1>(evalResult);
    bool emptyMacroBody = false;
    if (macroBody.empty()) {
      emptyMacroBody = true;
      macroBody.append(nbCRinArgs, '\n');
    }

    m_pp->append(macroBody);

    if (openingIndex >= 0) {
      LineColumn sourceLineColumnEnd = m_pp->getCurrentPosition();
      if (emptyMacroBody && nbCRinArgs) sourceLineColumnEnd.first -= nbCRinArgs;
      m_pp->getSourceFile()->addIncludeFileInfo(
          /* context */ IncludeFileInfo::Context::MACRO,
          /* action */ IncludeFileInfo::Action::POP,
          /* macroDefinition */ nullptr,
          /* sectionFileId */ BadPathId,
          /* sectionLine */ 0,
          /* sectionColumn */ 0,
          /* sourceLine */ sourceLineColumnEnd.first,
          /* sourceColumn */ sourceLineColumnEnd.second,
          /* sectionSymbolId */ BadSymbolId,
          /* symbolLine */ elc.first,
          /* symbolColumn */ elc.second,
          /* indexOpposite */ openingIndex);
    }
  } else {
    const std::string macroArgs = ctx->macro_actual_args()->getText();
    int32_t nbCRinArgs = std::count(macroArgs.begin(), macroArgs.end(), '\n');
    m_pp->append(std::string(nbCRinArgs, '\n'));
  }
}

void SV3_1aPpParseTreeListener::exitMacroInstanceWithArgs(
    SV3_1aPpParser::MacroInstanceWithArgsContext *ctx) {
  if (--m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppMacro_instance);
}

void SV3_1aPpParseTreeListener::enterMacroInstanceNoArgs(
    SV3_1aPpParser::MacroInstanceNoArgsContext *ctx) {
  if (m_paused++ == 0) appendPreprocBegin();
  if (!(m_inActiveBranch && !m_inMacroDefinitionParsing)) return;

  LineColumn slc = ParseUtils::getLineColumn(m_pp->getTokenStream(), ctx);
  LineColumn elc = ParseUtils::getEndLineColumn(m_pp->getTokenStream(), ctx);

  std::string macroName;
  if (antlr4::tree::TerminalNode *const macroIdentifierNode =
          ctx->Macro_identifier()) {
    macroName = macroIdentifierNode->getText();
    slc = ParseUtils::getLineColumn(macroIdentifierNode);
    elc = ParseUtils::getEndLineColumn(macroIdentifierNode);
  } else if (antlr4::tree::TerminalNode *const macroEscapedIdentifierNode =
                 ctx->Macro_Escaped_identifier()) {
    macroName = macroEscapedIdentifierNode->getText();
    std::string_view svname = macroName;
    svname.remove_prefix(1);
    macroName = StringUtils::rtrim(svname);
    slc = ParseUtils::getLineColumn(macroEscapedIdentifierNode);
    elc = ParseUtils::getEndLineColumn(macroEscapedIdentifierNode);
  }
  macroName.erase(macroName.begin());

  if (!m_pp->isMacroBody()) {
    m_pp->getSourceFile()->m_loopChecker.clear();
  }

  SymbolTable *const symbols = m_session->getSymbolTable();

  int32_t openingIndex = -1;
  std::vector<std::string> args;
  MacroInfo *const macroInfo = m_pp->getMacro(macroName);
  std::tuple<bool, std::string, std::vector<LineColumn>> evalResult;
  if (macroInfo != nullptr) {
    if (!macroInfo->m_arguments.empty()) {
      Location loc(m_pp->getFileId(slc.first), m_pp->getLineNb(slc.first),
                   slc.second, symbols->getId(macroName));
      Location extraLoc(macroInfo->m_fileId, macroInfo->m_startLine,
                        macroInfo->m_startColumn);
      logError(ErrorDefinition::PP_MACRO_PARENTHESIS_NEEDED, loc, extraLoc);
    }

    if (m_pp == m_pp->getSourceFile()) {
      PathId fileId = m_pp->getRawFileId();
      if (!fileId && m_pp->getEmbeddedMacroCallFile()) {
        fileId = m_pp->getEmbeddedMacroCallFile();
        slc.first += m_pp->getEmbeddedMacroCallLine() - 1;
        elc.first += m_pp->getEmbeddedMacroCallLine() - 1;
      }

      const LineColumn sourceLineColumnStart = m_pp->getCurrentPosition();
      openingIndex = m_pp->getSourceFile()->addIncludeFileInfo(
          /* context */ IncludeFileInfo::Context::MACRO,
          /* action */ IncludeFileInfo::Action::PUSH,
          /* macroDefinition */ macroInfo,
          /* sectionFileId */ fileId,
          /* sectionLine */ macroInfo->m_startLine,
          /* sectionColumn */ macroInfo->m_bodyStartColumn,
          /* sourceLine */ sourceLineColumnStart.first,
          /* sourceColumn */ sourceLineColumnStart.second,
          /* sectionSymbolId */ BadSymbolId,
          /* symbolLine */ slc.first,
          /* symbolColumn */ slc.second);
    }
    evalResult = m_pp->getMacro(
        macroName, args, m_pp, slc.first, m_pp->getSourceFile()->m_loopChecker,
        m_pp->m_instructions, macroInfo->m_fileId, macroInfo->m_startLine);
  } else {
    evalResult = m_pp->getMacro(macroName, args, m_pp, slc.first,
                                m_pp->getSourceFile()->m_loopChecker,
                                m_pp->m_instructions);
  }
  std::string &macroBody = std::get<1>(evalResult);
  if (macroBody.empty() && m_instructions.m_mark_empty_macro) {
    macroBody = SymbolTable::getEmptyMacroMarker();
  }

  m_pp->append(macroBody);

  if (openingIndex >= 0) {
    LineColumn sourceLineColumnEnd = m_pp->getCurrentPosition();
    m_pp->getSourceFile()->addIncludeFileInfo(
        /* context */ IncludeFileInfo::Context::MACRO,
        /* action */ IncludeFileInfo::Action::POP,
        /* macroDefinition */ nullptr,
        /* sectionFileId */ BadPathId,
        /* sectionLine */ 0,
        /* sectionColumn */ 0,
        /* sourceLine */ sourceLineColumnEnd.first,
        /* sourceColumn */ sourceLineColumnEnd.second,
        /* sectionSymbolId */ BadSymbolId,
        /* symbolLine */ elc.first,
        /* symbolColumn */ elc.second,
        /* indexOpposite */ openingIndex);
  }
}

void SV3_1aPpParseTreeListener::exitMacroInstanceNoArgs(
    SV3_1aPpParser::MacroInstanceNoArgsContext *ctx) {
  if (--m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppMacro_instance);
}

void SV3_1aPpParseTreeListener::recordMacro(
    std::string_view name, MacroInfo::DefType defType,
    std::string_view arguments, antlr4::ParserRuleContext *ctx,
    antlr4::tree::TerminalNode *identifier, antlr4::ParserRuleContext *body) {
  const LineColumn slc = ParseUtils::getLineColumn(m_pp->getTokenStream(), ctx);
  const LineColumn elc =
      ParseUtils::getEndLineColumn(m_pp->getTokenStream(), ctx);
  const LineColumn nslc = ParseUtils::getLineColumn(identifier);

  LineColumn bslc;
  std::vector<std::string> bodyTokens;
  std::vector<LineColumn> bodyTokenPositions;
  if (body != nullptr) {
    bslc = ParseUtils::getLineColumn(m_pp->getTokenStream(), body);

    std::vector<antlr4::Token *> tokens = ParseUtils::getFlatTokenList(body);
    bodyTokens.reserve(tokens.size());
    bodyTokenPositions.reserve(tokens.size());
    for (antlr4::Token *token : tokens) {
      bodyTokens.emplace_back(token->getText());
      bodyTokenPositions.emplace_back(ParseUtils::getLineColumn(token));
    }
  }

  m_pp->defineMacro(name, defType, m_pp->getLineNb(slc.first), slc.second,
                    m_pp->getLineNb(elc.first), elc.second, nslc.second,
                    bslc.second, arguments, {}, bodyTokens, bodyTokenPositions);
}

void SV3_1aPpParseTreeListener::enterMacro_definition(
    SV3_1aPpParser::Macro_definitionContext *ctx) {
  if (m_paused++ == 0) appendPreprocBegin();
  if (!m_inActiveBranch) return;

  std::string macroName;
  std::string arguments;
  antlr4::tree::TerminalNode *identifier = nullptr;
  antlr4::ParserRuleContext *body = nullptr;

  if (SV3_1aPpParser::Simple_no_args_macro_definitionContext *const
          simpleNoArgsDefinition = ctx->simple_no_args_macro_definition()) {
    if ((identifier = simpleNoArgsDefinition->Simple_identifier()))
      macroName = identifier->getText();
    else if ((identifier = simpleNoArgsDefinition->ESCAPED_IDENTIFIER())) {
      macroName = identifier->getText();
      std::string_view svname = macroName;
      svname.remove_prefix(1);
      macroName = StringUtils::rtrim(svname);
    }

    body = simpleNoArgsDefinition->simple_macro_definition_body();
  } else if (SV3_1aPpParser::Simple_args_macro_definitionContext *const
                 simpleArgsDefinition = ctx->simple_args_macro_definition()) {
    if ((identifier = simpleArgsDefinition->Simple_identifier()))
      macroName = identifier->getText();
    else if ((identifier = simpleArgsDefinition->ESCAPED_IDENTIFIER())) {
      macroName = identifier->getText();
      std::string_view svname = macroName;
      svname.remove_prefix(1);
      macroName = StringUtils::rtrim(svname);
    }

    arguments = simpleArgsDefinition->macro_arguments()->getText();
    body = simpleArgsDefinition->simple_macro_definition_body();
  } else if (SV3_1aPpParser::Multiline_no_args_macro_definitionContext
                 *const multiNoArgsDefinition =
                     ctx->multiline_no_args_macro_definition()) {
    if ((identifier = multiNoArgsDefinition->Simple_identifier()))
      macroName = identifier->getText();
    else if ((identifier = multiNoArgsDefinition->ESCAPED_IDENTIFIER())) {
      macroName = identifier->getText();
      std::string_view svname = macroName;
      svname.remove_prefix(1);
      macroName = StringUtils::rtrim(svname);
    }

    body = multiNoArgsDefinition->escaped_macro_definition_body();
  } else if (SV3_1aPpParser::Multiline_args_macro_definitionContext *const
                 multiArgsDefinition = ctx->multiline_args_macro_definition()) {
    if ((identifier = multiArgsDefinition->Simple_identifier()))
      macroName = identifier->getText();
    else if ((identifier = multiArgsDefinition->ESCAPED_IDENTIFIER())) {
      macroName = identifier->getText();
      std::string_view svname = macroName;
      svname.remove_prefix(1);
      macroName = StringUtils::rtrim(svname);
    }

    arguments = multiArgsDefinition->macro_arguments()->getText();
    body = multiArgsDefinition->escaped_macro_definition_body();
  } else if (SV3_1aPpParser::Define_directiveContext *const defineDirective =
                 ctx->define_directive()) {
    if ((identifier = defineDirective->Simple_identifier()))
      macroName = identifier->getText();
    else if ((identifier = defineDirective->ESCAPED_IDENTIFIER())) {
      macroName = identifier->getText();
      std::string_view svname = macroName;
      svname.remove_prefix(1);
      macroName = StringUtils::rtrim(svname);
    }
  }

  std::string_view svname = macroName;
  if (macroName[0] == '\\') svname.remove_prefix(1);
  macroName = StringUtils::rtrim(svname);

  recordMacro(macroName, MacroInfo::DefType::DEFINE, arguments, ctx, identifier,
              body);
  m_inMacroDefinitionParsing = true;
}

void SV3_1aPpParseTreeListener::exitMacro_definition(
    SV3_1aPpParser::Macro_definitionContext *ctx) {
  if (--m_paused == 0) appendPreprocEnd(ctx, VObjectType::ppMacro_definition);
  if (!m_inActiveBranch) return;

  if ((ctx->multiline_args_macro_definition() != nullptr) ||
      (ctx->multiline_no_args_macro_definition() != nullptr)) {
    const std::string text = ctx->getText();
    m_pendingCRs += std::count(text.begin(), text.end(), '\n');
  }
  m_inMacroDefinitionParsing = false;
}

void SV3_1aPpParseTreeListener::exitSimple_macro_definition_body(
    SV3_1aPpParser::Simple_macro_definition_bodyContext *ctx) {
  addVObject(ctx, ctx->getText(), VObjectType::ppSimple_macro_definition_body);
}

void SV3_1aPpParseTreeListener::exitEscaped_macro_definition_body(
    SV3_1aPpParser::Escaped_macro_definition_bodyContext *ctx) {
  addVObject(ctx, ctx->getText(), VObjectType::ppEscaped_macro_definition_body);
}

void SV3_1aPpParseTreeListener::exitMacro_arg(
    SV3_1aPpParser::Macro_argContext *ctx) {
  addVObject(ctx, ctx->getText(), VObjectType::ppMacro_arg);
}

void SV3_1aPpParseTreeListener::enterEveryRule(antlr4::ParserRuleContext *ctx) {
  m_callstack.emplace_back(ctx->getRuleIndex());
}

void SV3_1aPpParseTreeListener::exitEveryRule(antlr4::ParserRuleContext *ctx) {
  if (!m_callstack.empty() && (m_callstack.back() == ctx->getRuleIndex())) {
    m_callstack.pop_back();
  }

  if (!m_visitedRules.emplace(ctx).second) return;

  if (isAnyOnCallStack({SV3_1aPpParser::RuleEscaped_macro_definition_body,
                        SV3_1aPpParser::RuleSimple_macro_definition_body,
                        SV3_1aPpParser::RuleMacro_arg,
                        SV3_1aPpParser::RuleIfdef_directive_in_macro_body,
                        SV3_1aPpParser::RuleIfndef_directive_in_macro_body,
                        SV3_1aPpParser::RuleElsif_directive_in_macro_body,
                        SV3_1aPpParser::RuleElseif_directive_in_macro_body})) {
    return;
  }

  // clang-format off
  switch (ctx->getRuleIndex()) {
<RULE_CASE_STATEMENTS>
    default: break;
   }
  // clang-format on
}

void SV3_1aPpParseTreeListener::visitTerminal(
    antlr4::tree::TerminalNode *node) {
  const antlr4::Token *const token = node->getSymbol();
  if (token->getType() == antlr4::Token::EOF) return;

  if (isAnyOnCallStack({SV3_1aPpParser::RuleEscaped_macro_definition_body,
                        SV3_1aPpParser::RuleSimple_macro_definition_body,
                        SV3_1aPpParser::RuleMacro_arg,
                        SV3_1aPpParser::RuleIfdef_directive_in_macro_body,
                        SV3_1aPpParser::RuleIfndef_directive_in_macro_body,
                        SV3_1aPpParser::RuleElsif_directive_in_macro_body,
                        SV3_1aPpParser::RuleElseif_directive_in_macro_body})) {
    return;
  }

  const bool directiveOnCallstack = isAnyOnCallStack(
      {SV3_1aPpParser::RuleElse_directive, SV3_1aPpParser::RuleElseif_directive,
       SV3_1aPpParser::RuleElsif_directive, SV3_1aPpParser::RuleEndif_directive,
       SV3_1aPpParser::RuleIfdef_directive,
       SV3_1aPpParser::RuleIfndef_directive,
       SV3_1aPpParser::RuleInclude_directive,
       SV3_1aPpParser::RuleLine_directive, SV3_1aPpParser::RuleMacro_definition,
       SV3_1aPpParser::RuleMacro_instance,
       SV3_1aPpParser::RuleSv_file_directive,
       SV3_1aPpParser::RuleSv_line_directive,
       SV3_1aPpParser::RuleUndef_directive});

  if (!m_inActiveBranch && !m_pp->getRawFileId()) {
    const std::string text = node->getText();
    m_pendingCRs += std::count(text.begin(), text.end(), '\n');
    if (m_pendingCRs > 0) {
      m_pp->append(std::string(m_pendingCRs, '\n'));
      m_pendingCRs = 0;
    }
    return;
  } else if (!directiveOnCallstack) {
    if (token->getType() != SV3_1aPpParser::ESCAPED_IDENTIFIER) {
      m_pp->append(node->getText());
    }
  } else if ((token->getType() == SV3_1aPpParser::CR) ||
             (token->getType() == SV3_1aPpParser::ESCAPED_CR)) {
    ++m_pendingCRs;
  }

  // clang-format off
  switch (token->getType()) {
<VISIT_CASE_STATEMENTS>
    default: break;
  }
  // clang-format on
}
}  // namespace SURELOG
