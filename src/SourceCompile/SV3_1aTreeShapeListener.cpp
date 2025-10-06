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
 * File:   SV3_1aTreeShapeListener.cpp
 * Author: alain
 *
 * Created on April 16, 2017, 8:28 PM
 */

#include "Surelog/SourceCompile/SV3_1aTreeShapeListener.h"

#include <antlr4-runtime.h>

#include <cctype>
#include <cstdint>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "Surelog/CommandLine/CommandLineParser.h"
#include "Surelog/Common/FileSystem.h"
#include "Surelog/Common/PathId.h"
#include "Surelog/Common/Session.h"
#include "Surelog/Common/SymbolId.h"
#include "Surelog/Design/Design.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/ErrorReporting/ErrorDefinition.h"
#include "Surelog/SourceCompile/CompilationUnit.h"
#include "Surelog/SourceCompile/Compiler.h"
#include "Surelog/SourceCompile/IncludeFileInfo.h"
#include "Surelog/SourceCompile/ParseFile.h"
#include "Surelog/SourceCompile/SymbolTable.h"
#include "Surelog/SourceCompile/VObjectTypes.h"
#include "Surelog/Utils/ParseUtils.h"
#include "Surelog/Utils/StringUtils.h"

namespace SURELOG {

void SV3_1aTreeShapeListener::enterTop_level_rule(
    SV3_1aParser::Top_level_ruleContext * /*ctx*/) {
  if (m_pf->getFileContent() == nullptr) {
    m_fileContent = new FileContent(m_session, m_pf->getFileId(0),
                                    m_pf->getLibrary(), nullptr, BadPathId);
    m_pf->setFileContent(m_fileContent);
    m_pf->getCompileSourceFile()->getCompiler()->getDesign()->addFileContent(
        m_pf->getFileId(0), m_fileContent);
  } else {
    m_fileContent = m_pf->getFileContent();
  }
  CommandLineParser *clp = m_session->getCommandLineParser();
  if ((!clp->parseOnly()) && (!clp->lowMem())) {
    m_includeFileInfo.emplace(IncludeFileInfo::Context::None,
                              IncludeFileInfo::Action::Push, m_pf->getFileId(0),
                              0, 0, 0, 0, BadSymbolId, 0, 0);
  }
}

void SV3_1aTreeShapeListener::enterTop_level_library_rule(
    SV3_1aParser::Top_level_library_ruleContext * /*ctx*/) {
  // Visited from Library/SVLibShapeListener.h
  m_fileContent = new FileContent(m_session, m_pf->getFileId(0),
                                  m_pf->getLibrary(), nullptr, BadPathId);
  m_pf->setFileContent(m_fileContent);
}

void SV3_1aTreeShapeListener::enterModule_declaration(
    SV3_1aParser::Module_declarationContext *ctx) {
  std::string ident;
  if (ctx->module_ansi_header())
    ident = ctx->module_ansi_header()->identifier()->getText();
  else if (ctx->module_nonansi_header())
    ident = ctx->module_nonansi_header()->identifier()->getText();
  else {
    if (ctx->identifier(0))
      ident = ctx->identifier(0)->getText();
    else
      ident = "MODULE NAME UNKNOWN";
  }
  ident = std::regex_replace(ident, m_regexEscSeqReplace, "");
  addNestedDesignElement(ctx, ident, DesignElement::Module,
                         VObjectType::paModule_keyword);
}

void SV3_1aTreeShapeListener::exitModule_declaration(
    SV3_1aParser::Module_declarationContext *ctx) {
  if (ctx->ENDMODULE() != nullptr) {
    addVObject((antlr4::ParserRuleContext *)ctx->ENDMODULE(),
               VObjectType::ENDMODULE);
  }
  addVObject(ctx, VObjectType::paModule_declaration);
  m_nestedElements.pop();
}

void SV3_1aTreeShapeListener::exitModule_keyword(
    SV3_1aParser::Module_keywordContext *ctx) {
  if (ctx->MODULE() != nullptr) {
    addVObject(ctx, ctx->MODULE()->getText(), VObjectType::paModule_keyword);
  } else if (ctx->MACROMODULE() != nullptr) {
    addVObject(ctx, ctx->MACROMODULE()->getText(),
               VObjectType::paModule_keyword);
  }
}

void SV3_1aTreeShapeListener::exitClass_constructor_declaration(
    SV3_1aParser::Class_constructor_declarationContext *ctx) {
  if (ctx->ENDFUNCTION())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDFUNCTION(),
               VObjectType::ENDFUNCTION);
  addVObject(ctx, VObjectType::paClass_constructor_declaration);
}

void SV3_1aTreeShapeListener::exitFunction_body_declaration(
    SV3_1aParser::Function_body_declarationContext *ctx) {
  if (ctx->ENDFUNCTION())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDFUNCTION(),
               VObjectType::ENDFUNCTION);
  addVObject(ctx, VObjectType::paFunction_body_declaration);
}

void SV3_1aTreeShapeListener::exitTask_body_declaration(
    SV3_1aParser::Task_body_declarationContext *ctx) {
  if (ctx->ENDTASK())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDTASK(),
               VObjectType::ENDTASK);
  addVObject(ctx, VObjectType::paTask_body_declaration);
}

void SV3_1aTreeShapeListener::exitJump_statement(
    SV3_1aParser::Jump_statementContext *ctx) {
  if (ctx->BREAK()) {
    addVObject((antlr4::ParserRuleContext *)ctx->BREAK(), VObjectType::BREAK);
  } else if (ctx->CONTINUE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->CONTINUE(),
               VObjectType::CONTINUE);
  } else if (ctx->RETURN()) {
    addVObject((antlr4::ParserRuleContext *)ctx->RETURN(), VObjectType::RETURN);
  }
  addVObject(ctx, VObjectType::paJump_statement);
}

void SV3_1aTreeShapeListener::exitClass_declaration(
    SV3_1aParser::Class_declarationContext *ctx) {
  if (ctx->CLASS())
    addVObject((antlr4::ParserRuleContext *)ctx->CLASS(), VObjectType::CLASS);
  if (ctx->VIRTUAL())
    addVObject((antlr4::ParserRuleContext *)ctx->VIRTUAL(),
               VObjectType::VIRTUAL);
  if (ctx->IMPLEMENTS())
    addVObject((antlr4::ParserRuleContext *)ctx->IMPLEMENTS(),
               VObjectType::IMPLEMENTS);
  if (ctx->EXTENDS())
    addVObject((antlr4::ParserRuleContext *)ctx->EXTENDS(),
               VObjectType::EXTENDS);
  if (ctx->ENDCLASS())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDCLASS(),
               VObjectType::ENDCLASS);
  addVObject(ctx, VObjectType::paClass_declaration);
}

void SV3_1aTreeShapeListener::exitInterface_class_declaration(
    SV3_1aParser::Interface_class_declarationContext *ctx) {
  if (ctx->INTERFACE())
    addVObject((antlr4::ParserRuleContext *)ctx->INTERFACE(),
               VObjectType::INTERFACE);
  if (ctx->EXTENDS())
    addVObject((antlr4::ParserRuleContext *)ctx->EXTENDS(),
               VObjectType::EXTENDS);
  if (ctx->ENDCLASS())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDCLASS(),
               VObjectType::ENDCLASS);
  addVObject(ctx, VObjectType::paInterface_class_declaration);
}

void SV3_1aTreeShapeListener::exitChecker_declaration(
    SV3_1aParser::Checker_declarationContext *ctx) {
  if (ctx->ENDCHECKER())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDCHECKER(),
               VObjectType::ENDCHECKER);
  addVObject(ctx, VObjectType::paChecker_declaration);
}

void SV3_1aTreeShapeListener::enterSlline(
    SV3_1aParser::SllineContext * /*ctx*/) {}

void SV3_1aTreeShapeListener::exitSlline(SV3_1aParser::SllineContext *ctx) {
  SymbolTable *const symbols = m_session->getSymbolTable();
  FileSystem *const fileSystem = m_session->getFileSystem();
  uint32_t startLine = std::stoi(ctx->INTEGRAL_NUMBER()[0]->getText());
  IncludeFileInfo::Action action = static_cast<IncludeFileInfo::Action>(
      std::stoi(ctx->INTEGRAL_NUMBER()[1]->getText()));
  std::string text(StringUtils::unquoted(ctx->QUOTED_STRING()->getText()));
  std::vector<std::string_view> parts;
  StringUtils::tokenize(text, "^", parts);
  std::string_view symbol = StringUtils::unquoted(parts[0]);
  std::string_view file = StringUtils::unquoted(parts[1]);

  LineColumn startLineCol = ParseUtils::getLineColumn(m_tokens, ctx);
  LineColumn endLineCol = ParseUtils::getEndLineColumn(m_tokens, ctx);
  if (action == IncludeFileInfo::Action::Push) {
    // Push
    m_includeFileInfo.emplace(IncludeFileInfo::Context::Include,
                              IncludeFileInfo::Action::Push,
                              fileSystem->toPathId(file, symbols), startLine, 0,
                              0, 0, symbols->registerSymbol(symbol),
                              startLineCol.first, startLineCol.second);
  } else if (action == IncludeFileInfo::Action::Pop) {
    // Pop
    if (!m_includeFileInfo.empty()) m_includeFileInfo.pop();
    if (!m_includeFileInfo.empty()) {
      IncludeFileInfo &info = m_includeFileInfo.top();
      info.m_symbolId = symbols->registerSymbol(symbol);
      info.m_sectionFileId = fileSystem->toPathId(file, symbols);
      info.m_sourceLine = startLineCol.first /*+ m_lineOffset */;
      info.m_sourceColumn = startLineCol.second /*+ m_lineOffset */;
      info.m_sectionLine = startLine;
      info.m_symbolLine = endLineCol.first;
      info.m_symbolColumn = endLineCol.second;
      info.m_action = IncludeFileInfo::Action::Pop;
    }
  }
}

void SV3_1aTreeShapeListener::enterInterface_declaration(
    SV3_1aParser::Interface_declarationContext *ctx) {
  std::string ident;
  if (ctx->interface_ansi_header()) {
    ident = ctx->interface_ansi_header()->interface_identifier()->getText();
    if (ctx->interface_ansi_header()->INTERFACE()) {
      addVObject((antlr4::ParserRuleContext *)ctx->interface_ansi_header()
                     ->INTERFACE(),
                 VObjectType::INTERFACE);
    }
  } else if (ctx->interface_nonansi_header()) {
    ident = ctx->interface_nonansi_header()->interface_identifier()->getText();
    if (ctx->interface_nonansi_header()->INTERFACE()) {
      addVObject((antlr4::ParserRuleContext *)ctx->interface_nonansi_header()
                     ->INTERFACE(),
                 VObjectType::INTERFACE);
    }
  } else {
    if (ctx->interface_identifier(0))
      ident = ctx->interface_identifier(0)->getText();
    else
      ident = "INTERFACE NAME UNKNOWN";
    if (ctx->INTERFACE()) {
      addVObject((antlr4::ParserRuleContext *)ctx->INTERFACE(),
                 VObjectType::INTERFACE);
    }
  }
  ident = std::regex_replace(ident, m_regexEscSeqReplace, "");
  addNestedDesignElement(ctx, ident, DesignElement::Interface,
                         VObjectType::INTERFACE);
}

void SV3_1aTreeShapeListener::exitInterface_declaration(
    SV3_1aParser::Interface_declarationContext *ctx) {
  if (ctx->EXTERN() == nullptr)
    if (ctx->ENDINTERFACE())
      addVObject((antlr4::ParserRuleContext *)ctx->ENDINTERFACE(),
                 VObjectType::ENDINTERFACE);
  addVObject(ctx, VObjectType::paInterface_declaration);
  m_nestedElements.pop();
}

void SV3_1aTreeShapeListener::exitProperty_expr(
    SV3_1aParser::Property_exprContext *ctx) {
  if (ctx->CASE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->CASE(), VObjectType::CASE);
  }
  if (ctx->ENDCASE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->ENDCASE(),
               VObjectType::ENDCASE);
  } else if (ctx->OR()) {
    addVObject((antlr4::ParserRuleContext *)ctx->OR(), VObjectType::OR);
  } else if (ctx->AND()) {
    addVObject((antlr4::ParserRuleContext *)ctx->AND(), VObjectType::AND);
  } else if (ctx->IF()) {
    addVObject((antlr4::ParserRuleContext *)ctx->IF(), VObjectType::IF);
  } else if (ctx->STRONG()) {
    addVObject((antlr4::ParserRuleContext *)ctx->STRONG(), VObjectType::STRONG);
  } else if (ctx->WEAK()) {
    addVObject((antlr4::ParserRuleContext *)ctx->WEAK(), VObjectType::WEAK);
  } else if (ctx->NOT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->NOT(), VObjectType::NOT);
  } else if (ctx->OVERLAP_IMPLY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->OVERLAP_IMPLY(),
               VObjectType::OVERLAP_IMPLY);
  } else if (ctx->NON_OVERLAP_IMPLY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->NON_OVERLAP_IMPLY(),
               VObjectType::NON_OVERLAP_IMPLY);
  } else if (ctx->OVERLAPPED()) {
    addVObject((antlr4::ParserRuleContext *)ctx->OVERLAPPED(),
               VObjectType::OVERLAPPED);
  } else if (ctx->NONOVERLAPPED()) {
    addVObject((antlr4::ParserRuleContext *)ctx->NONOVERLAPPED(),
               VObjectType::NONOVERLAPPED);
  } else if (ctx->S_NEXTTIME()) {
    addVObject((antlr4::ParserRuleContext *)ctx->S_NEXTTIME(),
               VObjectType::S_NEXTTIME);
  } else if (ctx->ALWAYS()) {
    addVObject((antlr4::ParserRuleContext *)ctx->ALWAYS(), VObjectType::ALWAYS);
  } else if (ctx->S_ALWAYS()) {
    addVObject((antlr4::ParserRuleContext *)ctx->S_ALWAYS(),
               VObjectType::S_ALWAYS);
  } else if (ctx->S_EVENTUALLY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->S_EVENTUALLY(),
               VObjectType::S_EVENTUALLY);
  } else if (ctx->EVENTUALLY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->EVENTUALLY(),
               VObjectType::EVENTUALLY);
  } else if (ctx->UNTIL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->UNTIL(), VObjectType::UNTIL);
  } else if (ctx->S_UNTIL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->S_UNTIL(),
               VObjectType::S_UNTIL);
  } else if (ctx->IMPLIES()) {
    addVObject((antlr4::ParserRuleContext *)ctx->IMPLIES(),
               VObjectType::IMPLIES);
  } else if (ctx->IFF()) {
    addVObject((antlr4::ParserRuleContext *)ctx->IFF(), VObjectType::IFF);
  } else if (ctx->ACCEPT_ON()) {
    addVObject((antlr4::ParserRuleContext *)ctx->ACCEPT_ON(),
               VObjectType::ACCEPT_ON);
  } else if (ctx->REJECT_ON()) {
    addVObject((antlr4::ParserRuleContext *)ctx->REJECT_ON(),
               VObjectType::REJECT_ON);
  } else if (ctx->SYNC_ACCEPT_ON()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SYNC_ACCEPT_ON(),
               VObjectType::SYNC_ACCEPT_ON);
  } else if (ctx->SYNC_REJECT_ON()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SYNC_REJECT_ON(),
               VObjectType::SYNC_REJECT_ON);
  }
  addVObject(ctx, VObjectType::paProperty_expr);
}

void SV3_1aTreeShapeListener::exitGenerate_module_case_statement(
    SV3_1aParser::Generate_module_case_statementContext *ctx) {
  if (ctx->ENDCASE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDCASE(),
               VObjectType::ENDCASE);
  addVObject(ctx, VObjectType::paGenerate_module_case_statement);
}

void SV3_1aTreeShapeListener::exitIf_generate_construct(
    SV3_1aParser::If_generate_constructContext *ctx) {
  if (ctx->IF())
    addVObject((antlr4::ParserRuleContext *)ctx->IF(), VObjectType::IF);
  if (ctx->ELSE())
    addVObject((antlr4::ParserRuleContext *)ctx->ELSE(), VObjectType::ELSE);
  addVObject(ctx, VObjectType::paIf_generate_construct);
}

void SV3_1aTreeShapeListener::exitGenerate_interface_case_statement(
    SV3_1aParser::Generate_interface_case_statementContext *ctx) {
  if (ctx->ENDCASE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDCASE(),
               VObjectType::ENDCASE);
  addVObject(ctx, VObjectType::paGenerate_interface_case_statement);
}

void SV3_1aTreeShapeListener::exitCase_generate_construct(
    SV3_1aParser::Case_generate_constructContext *ctx) {
  if (ctx->ENDCASE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDCASE(),
               VObjectType::ENDCASE);
  addVObject(ctx, VObjectType::paCase_generate_construct);
}

void SV3_1aTreeShapeListener::exitCase_statement(
    SV3_1aParser::Case_statementContext *ctx) {
  if (ctx->ENDCASE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDCASE(),
               VObjectType::ENDCASE);
  addVObject(ctx, VObjectType::paCase_statement);
}

void SV3_1aTreeShapeListener::exitRandcase_statement(
    SV3_1aParser::Randcase_statementContext *ctx) {
  if (ctx->ENDCASE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDCASE(),
               VObjectType::ENDCASE);
  addVObject(ctx, VObjectType::paRandcase_statement);
}

void SV3_1aTreeShapeListener::exitRs_case(SV3_1aParser::Rs_caseContext *ctx) {
  if (ctx->ENDCASE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDCASE(),
               VObjectType::ENDCASE);
  addVObject(ctx, VObjectType::paRs_case);
}

void SV3_1aTreeShapeListener::exitSequence_declaration(
    SV3_1aParser::Sequence_declarationContext *ctx) {
  if (ctx->ENDSEQUENCE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDSEQUENCE(),
               VObjectType::ENDSEQUENCE);
  addVObject(ctx, VObjectType::paSequence_declaration);
}

void SV3_1aTreeShapeListener::exitRandsequence_statement(
    SV3_1aParser::Randsequence_statementContext *ctx) {
  if (ctx->ENDSEQUENCE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDSEQUENCE(),
               VObjectType::ENDSEQUENCE);
  addVObject(ctx, VObjectType::paRandsequence_statement);
}

void SV3_1aTreeShapeListener::exitSeq_block(
    SV3_1aParser::Seq_blockContext *ctx) {
  if (ctx->END())
    addVObject((antlr4::ParserRuleContext *)ctx->END(), VObjectType::END);
  addVObject(ctx, VObjectType::paSeq_block);
}

void SV3_1aTreeShapeListener::exitGenerate_module_named_block(
    SV3_1aParser::Generate_module_named_blockContext *ctx) {
  if (ctx->END())
    addVObject((antlr4::ParserRuleContext *)ctx->END(), VObjectType::END);
  addVObject(ctx, VObjectType::paGenerate_module_named_block);
}

void SV3_1aTreeShapeListener::exitGenerate_module_block(
    SV3_1aParser::Generate_module_blockContext *ctx) {
  if (ctx->END())
    addVObject((antlr4::ParserRuleContext *)ctx->END(), VObjectType::END);
  addVObject(ctx, VObjectType::paGenerate_module_block);
}

void SV3_1aTreeShapeListener::exitGenerate_interface_named_block(
    SV3_1aParser::Generate_interface_named_blockContext *ctx) {
  if (ctx->END())
    addVObject((antlr4::ParserRuleContext *)ctx->END(), VObjectType::END);
  addVObject(ctx, VObjectType::paGenerate_interface_named_block);
}

void SV3_1aTreeShapeListener::exitGenerate_interface_block(
    SV3_1aParser::Generate_interface_blockContext *ctx) {
  if (ctx->END())
    addVObject((antlr4::ParserRuleContext *)ctx->END(), VObjectType::END);
  addVObject(ctx, VObjectType::paGenerate_interface_block);
}

void SV3_1aTreeShapeListener::exitGenerate_begin_end_block(
    SV3_1aParser::Generate_begin_end_blockContext *ctx) {
  if (ctx->END())
    addVObject((antlr4::ParserRuleContext *)ctx->END(), VObjectType::END);
  addVObject(ctx, VObjectType::paGenerate_begin_end_block);
}

void SV3_1aTreeShapeListener::exitNamed_port_connection(
    SV3_1aParser::Named_port_connectionContext *ctx) {
  if (ctx->DOTSTAR())
    addVObject((antlr4::ParserRuleContext *)ctx->DOTSTAR(),
               VObjectType::DOTSTAR);
  if (ctx->OPEN_PARENS())
    addVObject((antlr4::ParserRuleContext *)ctx->OPEN_PARENS(),
               VObjectType::OPEN_PARENS);
  if (ctx->CLOSE_PARENS())
    addVObject((antlr4::ParserRuleContext *)ctx->CLOSE_PARENS(),
               VObjectType::CLOSE_PARENS);
  addVObject(ctx, VObjectType::paNamed_port_connection);
}

void SV3_1aTreeShapeListener::exitPattern(SV3_1aParser::PatternContext *ctx) {
  if (ctx->DOT())
    addVObject((antlr4::ParserRuleContext *)ctx->DOT(), VObjectType::DOT);
  else if (ctx->DOTSTAR())
    addVObject((antlr4::ParserRuleContext *)ctx->DOTSTAR(),
               VObjectType::DOTSTAR);
  else if (ctx->TAGGED())
    addVObject((antlr4::ParserRuleContext *)ctx->TAGGED(), VObjectType::TAGGED);

  addVObject(ctx, VObjectType::paPattern);
}

void SV3_1aTreeShapeListener::exitSpecify_block(
    SV3_1aParser::Specify_blockContext *ctx) {
  if (ctx->ENDSPECIFY())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDSPECIFY(),
               VObjectType::ENDSPECIFY);
  addVObject(ctx, VObjectType::paSpecify_block);
}

void SV3_1aTreeShapeListener::exitConfig_declaration(
    SV3_1aParser::Config_declarationContext *ctx) {
  if (ctx->ENDCONFIG())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDCONFIG(),
               VObjectType::ENDCONFIG);
  addVObject(ctx, VObjectType::paConfig_declaration);
}

void SV3_1aTreeShapeListener::exitDpi_import_export(
    SV3_1aParser::Dpi_import_exportContext *ctx) {
  if (ctx->IMPORT())
    addVObject((antlr4::ParserRuleContext *)ctx->IMPORT(), VObjectType::IMPORT);
  if (ctx->EXPORT())
    addVObject((antlr4::ParserRuleContext *)ctx->EXPORT(), VObjectType::EXPORT);
  addVObject(ctx, VObjectType::paDpi_import_export);
}

void SV3_1aTreeShapeListener::exitProperty_declaration(
    SV3_1aParser::Property_declarationContext *ctx) {
  if (ctx->ENDPROPERTY())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDPROPERTY(),
               VObjectType::ENDPROPERTY);
  addVObject(ctx, VObjectType::paProperty_declaration);
}

void SV3_1aTreeShapeListener::exitCovergroup_declaration(
    SV3_1aParser::Covergroup_declarationContext *ctx) {
  if (ctx->ENDGROUP())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDGROUP(),
               VObjectType::ENDGROUP);
  addVObject(ctx, VObjectType::paCovergroup_declaration);
}

void SV3_1aTreeShapeListener::exitGenerated_module_instantiation(
    SV3_1aParser::Generated_module_instantiationContext *ctx) {
  if (ctx->ENDGENERATE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDGENERATE(),
               VObjectType::ENDGENERATE);
  addVObject(ctx, VObjectType::paGenerated_module_instantiation);
}

void SV3_1aTreeShapeListener::exitGenerated_interface_instantiation(
    SV3_1aParser::Generated_interface_instantiationContext *ctx) {
  if (ctx->ENDGENERATE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDGENERATE(),
               VObjectType::ENDGENERATE);
  addVObject(ctx, VObjectType::paGenerated_interface_instantiation);
}

void SV3_1aTreeShapeListener::exitGenerate_region(
    SV3_1aParser::Generate_regionContext *ctx) {
  if (ctx->ENDGENERATE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDGENERATE(),
               VObjectType::ENDGENERATE);
  addVObject(ctx, VObjectType::paGenerate_region);
}

void SV3_1aTreeShapeListener::exitUdp_declaration(
    SV3_1aParser::Udp_declarationContext *ctx) {
  if (ctx->ENDPRIMITIVE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDPRIMITIVE(),
               VObjectType::ENDPRIMITIVE);
  addVObject(ctx, VObjectType::paUdp_declaration);
}

void SV3_1aTreeShapeListener::exitCombinational_body(
    SV3_1aParser::Combinational_bodyContext *ctx) {
  if (ctx->ENDTABLE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDTABLE(),
               VObjectType::ENDTABLE);
  addVObject(ctx, VObjectType::paCombinational_body);
}

void SV3_1aTreeShapeListener::exitSequential_body(
    SV3_1aParser::Sequential_bodyContext *ctx) {
  if (ctx->ENDTABLE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDTABLE(),
               VObjectType::ENDTABLE);
  addVObject(ctx, VObjectType::paSequential_body);
}

void SV3_1aTreeShapeListener::exitClocking_declaration(
    SV3_1aParser::Clocking_declarationContext *ctx) {
  if (ctx->DEFAULT())
    addVObject((antlr4::ParserRuleContext *)ctx->DEFAULT(),
               VObjectType::DEFAULT);
  if (ctx->GLOBAL())
    addVObject((antlr4::ParserRuleContext *)ctx->GLOBAL(), VObjectType::GLOBAL);
  if (ctx->ENDCLOCKING())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDCLOCKING(),
               VObjectType::ENDCLOCKING);
  addVObject(ctx, VObjectType::paClocking_declaration);
}

void SV3_1aTreeShapeListener::exitPackage_declaration(
    SV3_1aParser::Package_declarationContext *ctx) {
  if (ctx->ENDPACKAGE())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDPACKAGE(),
               VObjectType::ENDPACKAGE);
  addVObject(ctx, VObjectType::paPackage_declaration);
}

void SV3_1aTreeShapeListener::enterProgram_declaration(
    SV3_1aParser::Program_declarationContext *ctx) {
  std::string ident;
  if (ctx->program_ansi_header()) {
    ident = ctx->program_ansi_header()->identifier()->getText();
    if (ctx->program_ansi_header()->PROGRAM()) {
      addVObject(
          (antlr4::ParserRuleContext *)ctx->program_ansi_header()->PROGRAM(),
          VObjectType::PROGRAM);
    }
  } else if (ctx->program_nonansi_header()) {
    ident = ctx->program_nonansi_header()->identifier()->getText();
    if (ctx->program_nonansi_header()->PROGRAM()) {
      addVObject(
          (antlr4::ParserRuleContext *)ctx->program_nonansi_header()->PROGRAM(),
          VObjectType::PROGRAM);
    }
  } else {
    if (ctx->identifier(0))
      ident = ctx->identifier(0)->getText();
    else
      ident = "PROGRAM NAME UNKNOWN";
    if (ctx->PROGRAM()) {
      addVObject((antlr4::ParserRuleContext *)ctx->PROGRAM(),
                 VObjectType::PROGRAM);
    }
  }
  ident = std::regex_replace(ident, m_regexEscSeqReplace, "");
  addDesignElement(ctx, ident, DesignElement::Program, VObjectType::PROGRAM);
}

void SV3_1aTreeShapeListener::enterClass_declaration(
    SV3_1aParser::Class_declarationContext *ctx) {
  std::string ident;
  if (ctx->identifier(0)) {
    ident = ctx->identifier(0)->getText();
    ident = std::regex_replace(ident, m_regexEscSeqReplace, "");
    addDesignElement(ctx, ident, DesignElement::Class, VObjectType::CLASS);
  } else
    addDesignElement(ctx, "UNNAMED_CLASS", DesignElement::Class,
                     VObjectType::CLASS);
}

SV3_1aTreeShapeListener::SV3_1aTreeShapeListener(
    Session *session, ParseFile *pf, antlr4::CommonTokenStream *tokens,
    uint32_t lineOffset)
    : SV3_1aTreeShapeHelper(session, pf, tokens, lineOffset) {}

SV3_1aTreeShapeListener::~SV3_1aTreeShapeListener() = default;

void SV3_1aTreeShapeListener::enterPackage_declaration(
    SV3_1aParser::Package_declarationContext *ctx) {
  if (ctx->PACKAGE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->PACKAGE(),
               VObjectType::PACKAGE);
  }
  std::string ident = ctx->identifier(0)->getText();
  ident = std::regex_replace(ident, m_regexEscSeqReplace, "");
  addDesignElement(ctx, ident, DesignElement::Package, VObjectType::PACKAGE);
}

void SV3_1aTreeShapeListener::enterTimeUnitsDecl_TimeUnit(
    SV3_1aParser::TimeUnitsDecl_TimeUnitContext *ctx) {
  if (m_currentElement) {
    m_currentElement->m_timeInfo.m_type = TimeInfo::Type::TimeUnitTimePrecision;
    auto pair = getTimeValue(ctx->time_literal());
    m_currentElement->m_timeInfo.m_timeUnitValue = pair.first;
    m_currentElement->m_timeInfo.m_timeUnit = pair.second;
  }
}

void SV3_1aTreeShapeListener::enterTimescale_directive(
    SV3_1aParser::Timescale_directiveContext *ctx) {
  TimeInfo compUnitTimeInfo;
  compUnitTimeInfo.m_type = TimeInfo::Type::Timescale;
  compUnitTimeInfo.m_fileId = m_pf->getFileId(0);
  LineColumn lineCol = ParseUtils::getLineColumn(ctx->TICK_TIMESCALE());
  compUnitTimeInfo.m_line = lineCol.first;
  std::regex base_regex("`timescale([0-9]+)([mnsupf]+)/([0-9]+)([mnsupf]+)");
  std::smatch base_match;
  const std::string value = ctx->getText();
  if (std::regex_match(value, base_match, base_regex)) {
    std::ssub_match base1_sub_match = base_match[1];
    std::string base1 = base1_sub_match.str();
    compUnitTimeInfo.m_timeUnitValue = std::stoi(base1);
    if ((compUnitTimeInfo.m_timeUnitValue != 1) &&
        (compUnitTimeInfo.m_timeUnitValue != 10) &&
        (compUnitTimeInfo.m_timeUnitValue != 100)) {
      logError(ErrorDefinition::PA_TIMESCALE_INVALID_VALUE, ctx, base1);
    }
    compUnitTimeInfo.m_timeUnit = TimeInfo::unitFromString(base_match[2].str());
    std::ssub_match base2_sub_match = base_match[3];
    std::string base2 = base2_sub_match.str();
    compUnitTimeInfo.m_timePrecisionValue = std::stoi(base2);
    if ((compUnitTimeInfo.m_timePrecisionValue != 1) &&
        (compUnitTimeInfo.m_timePrecisionValue != 10) &&
        (compUnitTimeInfo.m_timePrecisionValue != 100)) {
      logError(ErrorDefinition::PA_TIMESCALE_INVALID_VALUE, ctx, base2);
    }
    uint64_t unitInFs = TimeInfo::femtoSeconds(
        compUnitTimeInfo.m_timeUnit, compUnitTimeInfo.m_timeUnitValue);
    compUnitTimeInfo.m_timePrecision =
        TimeInfo::unitFromString(base_match[4].str());
    uint64_t precisionInFs =
        TimeInfo::femtoSeconds(compUnitTimeInfo.m_timePrecision,
                               compUnitTimeInfo.m_timePrecisionValue);
    if (unitInFs < precisionInFs) {
      logError(ErrorDefinition::PA_TIMESCALE_INVALID_SCALE, ctx, "");
    }
  }
  m_pf->getCompilationUnit()->recordTimeInfo(compUnitTimeInfo);
}

void SV3_1aTreeShapeListener::enterTimeUnitsDecl_TimePrecision(
    SV3_1aParser::TimeUnitsDecl_TimePrecisionContext *ctx) {
  if (m_currentElement) {
    m_currentElement->m_timeInfo.m_type = TimeInfo::Type::TimeUnitTimePrecision;
    auto pair = getTimeValue(ctx->time_literal());
    m_currentElement->m_timeInfo.m_timePrecisionValue = pair.first;
    m_currentElement->m_timeInfo.m_timePrecision = pair.second;
  }
}

void SV3_1aTreeShapeListener::exitTime_literal(
    SV3_1aParser::Time_literalContext *ctx) {
  auto pair = getTimeValue(ctx);
  uint64_t value = pair.first;
  if (ctx->INTEGRAL_NUMBER())
    addVObject((antlr4::ParserRuleContext *)ctx->INTEGRAL_NUMBER(),
               std::to_string(value), VObjectType::INT_CONST);
  else if (ctx->REAL_NUMBER())
    addVObject((antlr4::ParserRuleContext *)ctx->REAL_NUMBER(),
               std::to_string(value), VObjectType::INT_CONST);
  const std::string &s = ctx->time_unit()->getText();
  if ((s == "s") || (s == "ms") || (s == "us") || (s == "ns") || (s == "ps") ||
      (s == "fs")) {
  } else {
    logError(ErrorDefinition::COMP_ILLEGAL_TIMESCALE, ctx, s);
  }
  addVObject(ctx->time_unit(), s, VObjectType::paTime_unit);
  addVObject(ctx, VObjectType::paTime_literal);
}

void SV3_1aTreeShapeListener::exitTime_unit(
    SV3_1aParser::Time_unitContext *ctx) {
  const std::string &s = ctx->getText();
  if ((s == "s") || (s == "ms") || (s == "us") || (s == "ns") || (s == "ps") ||
      (s == "fs")) {
    addVObject(ctx, ctx->getText(), VObjectType::paTime_unit);
  } else {
    addVObject((antlr4::ParserRuleContext *)ctx->SIMPLE_IDENTIFIER(),
               ctx->getText(), VObjectType::STRING_CONST);
    addVObject(ctx, VObjectType::paName_of_instance);
  }
}

void SV3_1aTreeShapeListener::enterTimeUnitsDecl_TimeUnitTimePrecision(
    SV3_1aParser::TimeUnitsDecl_TimeUnitTimePrecisionContext *ctx) {
  if (m_currentElement) {
    m_currentElement->m_timeInfo.m_type = TimeInfo::Type::TimeUnitTimePrecision;
    auto pair = getTimeValue(ctx->time_literal(0));
    m_currentElement->m_timeInfo.m_timeUnitValue = pair.first;
    m_currentElement->m_timeInfo.m_timeUnit = pair.second;
    pair = getTimeValue(ctx->time_literal(1));
    m_currentElement->m_timeInfo.m_timePrecisionValue = pair.first;
    m_currentElement->m_timeInfo.m_timePrecision = pair.second;
  }
}

void SV3_1aTreeShapeListener::enterTimeUnitsDecl_TimeUnitDiv(
    SV3_1aParser::TimeUnitsDecl_TimeUnitDivContext *ctx) {
  if (m_currentElement) {
    m_currentElement->m_timeInfo.m_type = TimeInfo::Type::TimeUnitTimePrecision;
    auto pair = getTimeValue(ctx->time_literal(0));
    m_currentElement->m_timeInfo.m_timeUnitValue = pair.first;
    m_currentElement->m_timeInfo.m_timeUnit = pair.second;
    pair = getTimeValue(ctx->time_literal(1));
    m_currentElement->m_timeInfo.m_timePrecisionValue = pair.first;
    m_currentElement->m_timeInfo.m_timePrecision = pair.second;
  }
}

void SV3_1aTreeShapeListener::enterTimeUnitsDecl_TimePrecisionTimeUnit(
    SV3_1aParser::TimeUnitsDecl_TimePrecisionTimeUnitContext *ctx) {
  if (m_currentElement) {
    m_currentElement->m_timeInfo.m_type = TimeInfo::Type::TimeUnitTimePrecision;
    auto pair = getTimeValue(ctx->time_literal(1));
    m_currentElement->m_timeInfo.m_timeUnitValue = pair.first;
    m_currentElement->m_timeInfo.m_timeUnit = pair.second;
    pair = getTimeValue(ctx->time_literal(0));
    m_currentElement->m_timeInfo.m_timePrecisionValue = pair.first;
    m_currentElement->m_timeInfo.m_timePrecision = pair.second;
  }
}

void SV3_1aTreeShapeListener::enterUdp_declaration(
    SV3_1aParser::Udp_declarationContext *ctx) {
  std::string ident;
  if (ctx->udp_ansi_declaration()) {
    ident = ctx->udp_ansi_declaration()->identifier()->getText();
    if (ctx->udp_ansi_declaration()->PRIMITIVE()) {
      addVObject(
          (antlr4::ParserRuleContext *)ctx->udp_ansi_declaration()->PRIMITIVE(),
          VObjectType::PRIMITIVE);
    }
  } else if (ctx->udp_nonansi_declaration()) {
    ident = ctx->udp_nonansi_declaration()->identifier()->getText();
    if (ctx->udp_nonansi_declaration()->PRIMITIVE()) {
      addVObject((antlr4::ParserRuleContext *)ctx->udp_nonansi_declaration()
                     ->PRIMITIVE(),
                 VObjectType::PRIMITIVE);
    }
  } else {
    if (ctx->identifier(0)) {
      ident = ctx->identifier(0)->getText();
    } else {
      ident = "UDP NAME UNKNOWN";
    }
    if (ctx->PRIMITIVE()) {
      addVObject((antlr4::ParserRuleContext *)ctx->PRIMITIVE(),
                 VObjectType::PRIMITIVE);
    }
  }
  ident = std::regex_replace(ident, m_regexEscSeqReplace, "");
  addDesignElement(ctx, ident, DesignElement::Primitive,
                   VObjectType::PRIMITIVE);
}

void SV3_1aTreeShapeListener::enterSurelog_macro_not_defined(
    SV3_1aParser::Surelog_macro_not_definedContext *ctx) {
  std::string text = ctx->getText();
  text.erase(0, 26);
  text.erase(text.size() - 3, text.size() - 1);
  logError(ErrorDefinition::PA_UNKOWN_MACRO, ctx, text);
}

void SV3_1aTreeShapeListener::exitInitVal_Integral(
    SV3_1aParser::InitVal_IntegralContext *ctx) {
  auto number = ctx->INTEGRAL_NUMBER();
  addVObject(ctx, number->getText(),
             VObjectType::INT_CONST);  // TODO: Octal, Hexa...
}

void SV3_1aTreeShapeListener::exitScalar_Integral(
    SV3_1aParser::Scalar_IntegralContext *ctx) {
  auto number = ctx->INTEGRAL_NUMBER();
  addVObject(ctx, number->getText(),
             VObjectType::INT_CONST);  // TODO: Octal, Hexa...
}

void SV3_1aTreeShapeListener::exitUnbased_unsized_literal(
    SV3_1aParser::Unbased_unsized_literalContext *ctx) {
  std::string s = ctx->SIMPLE_IDENTIFIER()->getText();
  VObjectType type = VObjectType::paZ;
  if (s == "z" || s == "Z") {
    type = VObjectType::paZ;
  } else if (s == "x" || s == "X") {
    type = VObjectType::paX;
  }
  addVObject(ctx, s, type);
}

void SV3_1aTreeShapeListener::exitPound_delay_value(
    SV3_1aParser::Pound_delay_valueContext *ctx) {
  if (ctx->POUND_DELAY()) {
    addVObject(ctx, ctx->POUND_DELAY()->getText(), VObjectType::INT_CONST);
  } else if (ctx->POUND_POUND_DELAY()) {
    addVObject(ctx, ctx->POUND_POUND_DELAY()->getText(),
               VObjectType::POUND_POUND_DELAY);
  } else if (ctx->delay_value()) {
    const std::string text = ctx->delay_value()->getText();
    if (std::isdigit(text[0])) {
      addVObject(ctx, text, VObjectType::INT_CONST);
    } else {
      addVObject(ctx, text, VObjectType::STRING_CONST);
    }
  }
}

void SV3_1aTreeShapeListener::exitData_type(
    SV3_1aParser::Data_typeContext *ctx) {
  if (ctx->VIRTUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->VIRTUAL(),
               VObjectType::VIRTUAL);
  }

  std::string text;
  if ((ctx->class_scope() != nullptr) || (ctx->package_scope() != nullptr) ||
      (ctx->string_type() != nullptr)) {
    text = ctx->getText();
  } else {
    std::vector<SV3_1aParser::IdentifierContext *> idctxs = ctx->identifier();
    if (idctxs.size() == 1) {
      text = idctxs.front()->getText();
    }
  }

  if (text.empty()) {
    addVObject(ctx, VObjectType::paData_type);
  } else {
    // Remove the packed/unpacked dimensions from the name
    std::string_view stext = text;
    std::string_view::size_type npos = stext.find_first_of('#');
    if (npos == std::string_view::npos) npos = stext.find_first_of('[');
    if (npos != std::string_view::npos)
      stext.remove_suffix(stext.length() - npos);

    while (!stext.empty() && std::isblank(stext.back())) stext.remove_suffix(1);

    addVObject(ctx, stext, VObjectType::paData_type);
  }
}

void SV3_1aTreeShapeListener::exitInterface_identifier(
    SV3_1aParser::Interface_identifierContext *ctx) {
  if (ctx->constant_expression().empty()) {
    addVObject(ctx, ctx->getText(), VObjectType::paInterface_identifier);
  } else {
    addVObject(ctx, VObjectType::paInterface_identifier);
  }
}

void SV3_1aTreeShapeListener::exitData_type_or_void(
    SV3_1aParser::Data_type_or_voidContext *ctx) {
  if (ctx->VOID()) {
    addVObject(ctx, ctx->VOID()->getText(), VObjectType::paData_type_or_void);
  } else {
    addVObject(ctx, VObjectType::paData_type_or_void);
  }
}

void SV3_1aTreeShapeListener::exitString_value(
    SV3_1aParser::String_valueContext *ctx) {
  std::string ident;

  ident = ctx->QUOTED_STRING()->getText();

  std::smatch match;
  while (std::regex_search(ident, match, m_regexEscSeqSearch)) {
    std::string var = "\\" + match[1].str() + " ";
    ident = ident.replace(match.position(0), match.length(0), var);
  }

  addVObject(ctx, ident, VObjectType::STRING_LITERAL);

  if (ident.size() > SV_MAX_STRING_SIZE) {
    logError(ErrorDefinition::PA_MAX_LENGTH_IDENTIFIER, ctx, ident);
  }
}

void SV3_1aTreeShapeListener::exitIdentifier(
    SV3_1aParser::IdentifierContext *ctx) {
  std::string ident;
  if (ctx->SIMPLE_IDENTIFIER())
    ident = ctx->SIMPLE_IDENTIFIER()->getText();
  else if (ctx->ESCAPED_IDENTIFIER()) {
    ident = ctx->ESCAPED_IDENTIFIER()->getText();
    ident.erase(0, 3);
    ident.erase(ident.size() - 3, 3);
    ident = StringUtils::rtrim(ident);
  } else if (ctx->THIS())
    ident = ctx->THIS()->getText();
  else if (ctx->RANDOMIZE())
    ident = ctx->RANDOMIZE()->getText();
  else if (ctx->SAMPLE())
    ident = ctx->SAMPLE()->getText();

  // !!! Don't forget to change CompileModule.cpp type checker !!!
  addVObject(ctx, ident, VObjectType::STRING_CONST);

  if (ident.size() > SV_MAX_IDENTIFIER_SIZE) {
    logError(ErrorDefinition::PA_MAX_LENGTH_IDENTIFIER, ctx, ident);
  }
}

void SV3_1aTreeShapeListener::exitUnique_priority(
    SV3_1aParser::Unique_priorityContext *ctx) {
  if (ctx->PRIORITY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->PRIORITY(),
               VObjectType::PRIORITY);
  } else if (ctx->UNIQUE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->UNIQUE(), VObjectType::UNIQUE);
  } else if (ctx->UNIQUE0()) {
    addVObject((antlr4::ParserRuleContext *)ctx->UNIQUE0(),
               VObjectType::UNIQUE0);
  }
  addVObject(ctx, VObjectType::paUnique_priority);
}

void SV3_1aTreeShapeListener::exitCase_keyword(
    SV3_1aParser::Case_keywordContext *ctx) {
  if (ctx->CASE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->CASE(), VObjectType::CASE);
  } else if (ctx->CASEX()) {
    addVObject((antlr4::ParserRuleContext *)ctx->CASEX(), VObjectType::CASEX);
  } else if (ctx->CASEZ()) {
    addVObject((antlr4::ParserRuleContext *)ctx->CASEZ(), VObjectType::CASEZ);
  }
  addVObject(ctx, VObjectType::paCase_keyword);
}

void SV3_1aTreeShapeListener::exitPart_select_op_colon(
    SV3_1aParser::Part_select_op_colonContext *ctx) {
  if (ctx->INC_PART_SELECT_OP()) {
    addVObject(ctx, VObjectType::paIncPartSelectOp);
  } else if (ctx->DEC_PART_SELECT_OP()) {
    addVObject(ctx, VObjectType::paDecPartSelectOp);
  } else if (ctx->COLON()) {
    addVObject(ctx, VObjectType::paColonPartSelectOp);
  }
}

void SV3_1aTreeShapeListener::exitPart_select_op(
    SV3_1aParser::Part_select_opContext *ctx) {
  if (ctx->INC_PART_SELECT_OP()) {
    addVObject(ctx, VObjectType::paIncPartSelectOp);
  } else if (ctx->DEC_PART_SELECT_OP()) {
    addVObject(ctx, VObjectType::paDecPartSelectOp);
  }
}

void SV3_1aTreeShapeListener::exitSystem_task_names(
    SV3_1aParser::System_task_namesContext *ctx) {
  std::string ident = ctx->getText();
  if (ctx->TIME())
    addVObject((antlr4::ParserRuleContext *)ctx->TIME(), ident,
               VObjectType::STRING_CONST);
  else if (ctx->REALTIME())
    addVObject((antlr4::ParserRuleContext *)ctx->REALTIME(), ident,
               VObjectType::STRING_CONST);
  else if (ctx->ASSERT())
    addVObject((antlr4::ParserRuleContext *)ctx->ASSERT(), ident,
               VObjectType::STRING_CONST);
  else if (!ctx->SIMPLE_IDENTIFIER().empty())
    addVObject((antlr4::ParserRuleContext *)ctx->SIMPLE_IDENTIFIER()[0], ident,
               VObjectType::STRING_CONST);
  else if (ctx->SIGNED())
    addVObject((antlr4::ParserRuleContext *)ctx->SIGNED(), ident,
               VObjectType::STRING_CONST);
  else if (ctx->UNSIGNED())
    addVObject((antlr4::ParserRuleContext *)ctx->UNSIGNED(), ident,
               VObjectType::STRING_CONST);
  addVObject(ctx, VObjectType::paSystem_task_names);
}

void SV3_1aTreeShapeListener::exitClass_type(
    SV3_1aParser::Class_typeContext *ctx) {
  std::string ident;
  antlr4::ParserRuleContext *childCtx = nullptr;
  if (ctx->SIMPLE_IDENTIFIER()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->SIMPLE_IDENTIFIER();
    ident = ctx->SIMPLE_IDENTIFIER()->getText();
  } else if (ctx->ESCAPED_IDENTIFIER()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->ESCAPED_IDENTIFIER();
    ident = ctx->ESCAPED_IDENTIFIER()->getText();
    ident.erase(0, 3);
    ident.erase(ident.size() - 3, 3);
  } else if (ctx->THIS()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->THIS();
    ident = ctx->THIS()->getText();
  } else if (ctx->RANDOMIZE()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->RANDOMIZE();
    ident = ctx->RANDOMIZE()->getText();
  } else if (ctx->SAMPLE()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->SAMPLE();
    ident = ctx->SAMPLE()->getText();
  } else if (ctx->DOLLAR_UNIT()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->DOLLAR_UNIT();
    ident = ctx->DOLLAR_UNIT()->getText();
  }
  addVObject(childCtx, ident, VObjectType::STRING_CONST);
  addVObject(ctx, VObjectType::paClass_type);

  if (ident.size() > SV_MAX_IDENTIFIER_SIZE) {
    logError(ErrorDefinition::PA_MAX_LENGTH_IDENTIFIER, ctx, ident);
  }
}

void SV3_1aTreeShapeListener::exitHierarchical_identifier(
    SV3_1aParser::Hierarchical_identifierContext *ctx) {
  std::string ident;

  for (auto &o : ctx->children) {
    antlr4::tree::TerminalNode *tnode =
        dynamic_cast<antlr4::tree::TerminalNode *>(o);
    if (tnode != nullptr) {
      antlr4::Token *symbol = tnode->getSymbol();
      if (symbol->getType() == SV3_1aParser::SIMPLE_IDENTIFIER ||
          symbol->getType() == SV3_1aParser::ESCAPED_IDENTIFIER) {
        ident = tnode->getText();
        ident = std::regex_replace(ident, m_regexEscSeqReplace, "");
        addVObject((antlr4::ParserRuleContext *)tnode, ident,
                   VObjectType::STRING_CONST);
      } else if (symbol->getType() == SV3_1aParser::THIS ||
                 symbol->getType() == SV3_1aParser::RANDOMIZE ||
                 symbol->getType() == SV3_1aParser::SAMPLE) {
        ident = tnode->getText();
        addVObject((antlr4::ParserRuleContext *)tnode, ident,
                   VObjectType::STRING_CONST);
      }
    }
  }

  addVObject(ctx, VObjectType::paHierarchical_identifier);
  if (ident.size() > SV_MAX_IDENTIFIER_SIZE) {
    logError(ErrorDefinition::PA_MAX_LENGTH_IDENTIFIER, ctx, ident);
  }
}

void SV3_1aTreeShapeListener::exitFile_path_spec(
    SV3_1aParser::File_path_specContext *ctx) {
  std::string ident;
  ident = ctx->getText();

  addVObject(ctx, ident, VObjectType::STRING_CONST);

  if (ident.size() > SV_MAX_IDENTIFIER_SIZE) {
    logError(ErrorDefinition::PA_MAX_LENGTH_IDENTIFIER, ctx, ident);
  }
}

void SV3_1aTreeShapeListener::exitAnonymous_program(
    SV3_1aParser::Anonymous_programContext *ctx) {
  if (ctx->ENDPROGRAM())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDPROGRAM(),
               VObjectType::ENDPROGRAM);
  addVObject(ctx, VObjectType::paAnonymous_program);
}

void SV3_1aTreeShapeListener::exitProgram_declaration(
    SV3_1aParser::Program_declarationContext *ctx) {
  if (ctx->ENDPROGRAM())
    addVObject((antlr4::ParserRuleContext *)ctx->ENDPROGRAM(),
               VObjectType::ENDPROGRAM);
  addVObject(ctx, VObjectType::paProgram_declaration);
}

void SV3_1aTreeShapeListener::exitProcedural_continuous_assignment(
    SV3_1aParser::Procedural_continuous_assignmentContext *ctx) {
  if (ctx->ASSIGN()) {
    addVObject((antlr4::ParserRuleContext *)ctx->ASSIGN(), VObjectType::ASSIGN);
  } else if (ctx->DEASSIGN()) {
    addVObject((antlr4::ParserRuleContext *)ctx->DEASSIGN(),
               VObjectType::DEASSIGN);
  } else if (ctx->FORCE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->FORCE(), VObjectType::FORCE);
  } else if (ctx->RELEASE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->RELEASE(),
               VObjectType::RELEASE);
  }
  addVObject(ctx, VObjectType::paProcedural_continuous_assignment);
}

void SV3_1aTreeShapeListener::exitDrive_strength(
    SV3_1aParser::Drive_strengthContext *ctx) {
  if (ctx->SUPPLY0()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SUPPLY0(),
               VObjectType::SUPPLY0);
  } else if (ctx->STRONG0()) {
    addVObject((antlr4::ParserRuleContext *)ctx->STRONG0(),
               VObjectType::STRONG0);
  } else if (ctx->PULL0()) {
    addVObject((antlr4::ParserRuleContext *)ctx->PULL0(), VObjectType::PULL0);
  } else if (ctx->WEAK0()) {
    addVObject((antlr4::ParserRuleContext *)ctx->WEAK0(), VObjectType::WEAK0);
  }
  if (ctx->SUPPLY1()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SUPPLY1(),
               VObjectType::SUPPLY1);
  } else if (ctx->STRONG1()) {
    addVObject((antlr4::ParserRuleContext *)ctx->STRONG1(),
               VObjectType::STRONG1);
  } else if (ctx->PULL1()) {
    addVObject((antlr4::ParserRuleContext *)ctx->PULL1(), VObjectType::PULL1);
  } else if (ctx->WEAK1()) {
    addVObject((antlr4::ParserRuleContext *)ctx->WEAK1(), VObjectType::WEAK1);
  }

  if (ctx->HIGHZ0()) {
    addVObject((antlr4::ParserRuleContext *)ctx->HIGHZ0(), VObjectType::HIGHZ0);
  } else if (ctx->HIGHZ1()) {
    addVObject((antlr4::ParserRuleContext *)ctx->HIGHZ1(), VObjectType::HIGHZ1);
  }
  addVObject(ctx, VObjectType::paDrive_strength);
}

void SV3_1aTreeShapeListener::exitStrength0(
    SV3_1aParser::Strength0Context *ctx) {
  if (ctx->SUPPLY0()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SUPPLY0(),
               VObjectType::SUPPLY0);
  } else if (ctx->STRONG0()) {
    addVObject((antlr4::ParserRuleContext *)ctx->STRONG0(),
               VObjectType::STRONG0);
  } else if (ctx->PULL0()) {
    addVObject((antlr4::ParserRuleContext *)ctx->PULL0(), VObjectType::PULL0);
  } else if (ctx->WEAK0()) {
    addVObject((antlr4::ParserRuleContext *)ctx->WEAK0(), VObjectType::WEAK0);
  }
  addVObject(ctx, VObjectType::paStrength0);
}

void SV3_1aTreeShapeListener::exitAction_block(
    SV3_1aParser::Action_blockContext *ctx) {
  if (ctx->ELSE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->ELSE(), VObjectType::ELSE);
  }
  addVObject(ctx, VObjectType::paAction_block);
}

void SV3_1aTreeShapeListener::exitEvent_trigger(
    SV3_1aParser::Event_triggerContext *ctx) {
  if (ctx->IMPLY())
    addVObject((antlr4::ParserRuleContext *)ctx->IMPLY(),
               VObjectType::paBinOp_Imply);
  if (ctx->NON_BLOCKING_TRIGGER_EVENT_OP())
    addVObject(
        (antlr4::ParserRuleContext *)ctx->NON_BLOCKING_TRIGGER_EVENT_OP(),
        VObjectType::NON_BLOCKING_TRIGGER_EVENT_OP);
  addVObject(ctx, VObjectType::paEvent_trigger);
}

void SV3_1aTreeShapeListener::exitStrength1(
    SV3_1aParser::Strength1Context *ctx) {
  if (ctx->SUPPLY1()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SUPPLY1(),
               VObjectType::SUPPLY1);
  } else if (ctx->STRONG1()) {
    addVObject((antlr4::ParserRuleContext *)ctx->STRONG1(),
               VObjectType::STRONG1);
  } else if (ctx->PULL1()) {
    addVObject((antlr4::ParserRuleContext *)ctx->PULL1(), VObjectType::PULL1);
  } else if (ctx->WEAK1()) {
    addVObject((antlr4::ParserRuleContext *)ctx->WEAK1(), VObjectType::WEAK1);
  }
  addVObject(ctx, VObjectType::paStrength1);
}

void SV3_1aTreeShapeListener::exitCharge_strength(
    SV3_1aParser::Charge_strengthContext *ctx) {
  if (ctx->SMALL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SMALL(), VObjectType::SMALL);
  } else if (ctx->MEDIUM()) {
    addVObject((antlr4::ParserRuleContext *)ctx->MEDIUM(), VObjectType::MEDIUM);
  } else if (ctx->LARGE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->LARGE(), VObjectType::LARGE);
  }
  addVObject(ctx, VObjectType::paCharge_strength);
}

void SV3_1aTreeShapeListener::exitStream_operator(
    SV3_1aParser::Stream_operatorContext *ctx) {
  if (ctx->SHIFT_RIGHT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SHIFT_RIGHT(),
               VObjectType::paBinOp_ShiftRight);
  } else if (ctx->SHIFT_LEFT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SHIFT_LEFT(),
               VObjectType::paBinOp_ShiftLeft);
  }
  addVObject(ctx, VObjectType::paStream_operator);
}

void SV3_1aTreeShapeListener::exitLoop_statement(
    SV3_1aParser::Loop_statementContext *ctx) {
  if (ctx->DO()) {
    addVObject((antlr4::ParserRuleContext *)ctx->DO(), VObjectType::DO);
  } else if (ctx->FOREVER()) {
    addVObject((antlr4::ParserRuleContext *)ctx->FOREVER(),
               VObjectType::FOREVER);
  } else if (ctx->REPEAT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->REPEAT(), VObjectType::REPEAT);
  } else if (ctx->WHILE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->WHILE(), VObjectType::WHILE);
  } else if (ctx->FOR()) {
    addVObject((antlr4::ParserRuleContext *)ctx->FOR(), VObjectType::FOR);
  } else if (ctx->FOREACH()) {
    addVObject((antlr4::ParserRuleContext *)ctx->FOREACH(),
               VObjectType::FOREACH);
  }
  addVObject(ctx, VObjectType::paLoop_statement);
}

void SV3_1aTreeShapeListener::exitPackage_scope(
    SV3_1aParser::Package_scopeContext *ctx) {
  std::string ident;
  antlr4::ParserRuleContext *childCtx = nullptr;
  if (ctx->SIMPLE_IDENTIFIER()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->SIMPLE_IDENTIFIER();
    ident = ctx->SIMPLE_IDENTIFIER()->getText();
  } else if (ctx->ESCAPED_IDENTIFIER()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->ESCAPED_IDENTIFIER();
    ident = ctx->ESCAPED_IDENTIFIER()->getText();
    std::smatch match;
    while (std::regex_search(ident, match, m_regexEscSeqSearch)) {
      std::string var = match[1].str();
      ident = ident.replace(match.position(0), match.length(0), var);
    }
  } else if (ctx->THIS()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->THIS();
    ident = ctx->THIS()->getText();
  } else if (ctx->RANDOMIZE()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->RANDOMIZE();
    ident = ctx->RANDOMIZE()->getText();
  } else if (ctx->SAMPLE()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->SAMPLE();
    ident = ctx->SAMPLE()->getText();
  } else if (ctx->DOLLAR_UNIT()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->DOLLAR_UNIT();
    ident = ctx->DOLLAR_UNIT()->getText();
  }
  addVObject(childCtx, ident, VObjectType::STRING_CONST);
  addVObject(ctx, VObjectType::paPackage_scope);

  if (ident.size() > SV_MAX_IDENTIFIER_SIZE) {
    logError(ErrorDefinition::PA_MAX_LENGTH_IDENTIFIER, ctx, ident);
  }
}

void SV3_1aTreeShapeListener::exitPs_identifier(
    SV3_1aParser::Ps_identifierContext *ctx) {
  std::string ident;
  antlr4::ParserRuleContext *childCtx = nullptr;
  if (!ctx->SIMPLE_IDENTIFIER().empty()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->SIMPLE_IDENTIFIER()[0];
    ident = ctx->SIMPLE_IDENTIFIER()[0]->getText();
    if (ctx->SIMPLE_IDENTIFIER().size() > 1) {
      ident += "::" + ctx->SIMPLE_IDENTIFIER()[1]->getText();
    }
  } else if (!ctx->ESCAPED_IDENTIFIER().empty()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->ESCAPED_IDENTIFIER()[0];
    ident = ctx->ESCAPED_IDENTIFIER()[0]->getText();
    std::smatch match;
    while (std::regex_search(ident, match, m_regexEscSeqSearch)) {
      std::string var = match[1].str();
      ident = ident.replace(match.position(0), match.length(0), var);
    }
  } else if (!ctx->THIS().empty()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->THIS()[0];
    ident = ctx->THIS()[0]->getText();
  } else if (!ctx->RANDOMIZE().empty()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->RANDOMIZE()[0];
    ident = ctx->RANDOMIZE()[0]->getText();
  } else if (!ctx->SAMPLE().empty()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->SAMPLE()[0];
    ident = ctx->SAMPLE()[0]->getText();
  } else if (ctx->DOLLAR_UNIT()) {
    childCtx = (antlr4::ParserRuleContext *)ctx->DOLLAR_UNIT();
    ident = ctx->DOLLAR_UNIT()->getText();
  }
  addVObject(childCtx, ident, VObjectType::STRING_CONST);
  addVObject(ctx, VObjectType::paPs_identifier);

  if (ident.size() > SV_MAX_IDENTIFIER_SIZE) {
    logError(ErrorDefinition::PA_MAX_LENGTH_IDENTIFIER, ctx, ident);
  }
}

void SV3_1aTreeShapeListener::exitExpression(
    SV3_1aParser::ExpressionContext *ctx) {
  if (ctx->MATCHES()) {
    addVObject((antlr4::ParserRuleContext *)ctx->MATCHES(),
               VObjectType::paMatches);
  }
  if (ctx->PLUS()) {
    if (ctx->expression().size() == 1)
      addVObject((antlr4::ParserRuleContext *)ctx->PLUS(),
                 VObjectType::paUnary_Plus);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->PLUS(),
                 VObjectType::paBinOp_Plus);
  } else if (ctx->MINUS()) {
    if (ctx->expression().size() == 1)
      addVObject((antlr4::ParserRuleContext *)ctx->MINUS(),
                 VObjectType::paUnary_Minus);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->MINUS(),
                 VObjectType::paBinOp_Minus);
  } else if (ctx->BANG()) {
    addVObject((antlr4::ParserRuleContext *)ctx->BANG(),
               VObjectType::paUnary_Not);
  } else if (ctx->TILDA()) {
    addVObject((antlr4::ParserRuleContext *)ctx->TILDA(),
               VObjectType::paUnary_Tilda);
  } else if (ctx->BITW_AND()) {
    if (ctx->expression().size() == 1)
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_AND(),
                 VObjectType::paUnary_BitwAnd);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_AND(),
                 VObjectType::paBinOp_BitwAnd);
  } else if (ctx->BITW_OR()) {
    if (ctx->expression().size() == 1)
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_OR(),
                 VObjectType::paUnary_BitwOr);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_OR(),
                 VObjectType::paBinOp_BitwOr);
  } else if (ctx->BITW_XOR()) {
    if (ctx->expression().size() == 1)
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_XOR(),
                 VObjectType::paUnary_BitwXor);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_XOR(),
                 VObjectType::paBinOp_BitwXor);
  } else if (ctx->REDUCTION_NAND()) {
    if (ctx->expression().size() == 1)
      addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_NAND(),
                 VObjectType::paUnary_ReductNand);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_NAND(),
                 VObjectType::paBinOp_ReductNand);
  } else if (ctx->REDUCTION_NOR()) {
    addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_NOR(),
               VObjectType::paUnary_ReductNor);
  } else if (ctx->REDUCTION_XNOR1()) {
    if (ctx->expression().size() == 1)
      addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_XNOR1(),
                 VObjectType::paUnary_ReductXnor1);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_XNOR1(),
                 VObjectType::paBinOp_ReductXnor1);
  } else if (ctx->REDUCTION_XNOR2()) {
    if (ctx->expression().size() == 1)
      addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_XNOR2(),
                 VObjectType::paUnary_ReductXnor2);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_XNOR2(),
                 VObjectType::paBinOp_ReductXnor2);
  } else if (ctx->STARSTAR()) {
    addVObject((antlr4::ParserRuleContext *)ctx->STARSTAR(),
               VObjectType::paBinOp_MultMult);
  } else if (ctx->STAR()) {
    addVObject((antlr4::ParserRuleContext *)ctx->STAR(),
               VObjectType::paBinOp_Mult);
  } else if (ctx->DIV()) {
    addVObject((antlr4::ParserRuleContext *)ctx->DIV(),
               VObjectType::paBinOp_Div);
  } else if (ctx->PERCENT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->PERCENT(),
               VObjectType::paBinOp_Percent);
  } else if (ctx->SHIFT_RIGHT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SHIFT_RIGHT(),
               VObjectType::paBinOp_ShiftRight);
  } else if (ctx->SHIFT_LEFT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SHIFT_LEFT(),
               VObjectType::paBinOp_ShiftLeft);
  } else if (ctx->ARITH_SHIFT_RIGHT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->ARITH_SHIFT_RIGHT(),
               VObjectType::paBinOp_ArithShiftRight);
  } else if (ctx->ARITH_SHIFT_LEFT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->ARITH_SHIFT_LEFT(),
               VObjectType::paBinOp_ArithShiftLeft);
  } else if (ctx->LESS()) {
    addVObject((antlr4::ParserRuleContext *)ctx->LESS(),
               VObjectType::paBinOp_Less);
  } else if (ctx->LESS_EQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->LESS_EQUAL(),
               VObjectType::paBinOp_LessEqual);
  } else if (ctx->PLUSPLUS()) {
    addVObject((antlr4::ParserRuleContext *)ctx->PLUSPLUS(),
               VObjectType::paIncDec_PlusPlus);
  } else if (ctx->MINUSMINUS()) {
    addVObject((antlr4::ParserRuleContext *)ctx->MINUSMINUS(),
               VObjectType::paIncDec_MinusMinus);
  } else if (ctx->GREATER()) {
    addVObject((antlr4::ParserRuleContext *)ctx->GREATER(),
               VObjectType::paBinOp_Great);
  } else if (ctx->GREATER_EQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->GREATER_EQUAL(),
               VObjectType::paBinOp_GreatEqual);
  } else if (ctx->INSIDE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->INSIDE(), VObjectType::INSIDE);
  } else if (ctx->EQUIV()) {
    addVObject((antlr4::ParserRuleContext *)ctx->EQUIV(),
               VObjectType::paBinOp_Equiv);
  } else if (ctx->NOTEQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->NOTEQUAL(),
               VObjectType::paBinOp_Not);
  } else if (ctx->BINARY_WILDCARD_EQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->BINARY_WILDCARD_EQUAL(),
               VObjectType::paBinOp_WildcardEqual);
  } else if (ctx->BINARY_WILDCARD_NOTEQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->BINARY_WILDCARD_NOTEQUAL(),
               VObjectType::paBinOp_WildcardNotEqual);
  } else if (ctx->FOUR_STATE_LOGIC_EQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->FOUR_STATE_LOGIC_EQUAL(),
               VObjectType::paBinOp_FourStateLogicEqual);
  } else if (ctx->FOUR_STATE_LOGIC_NOTEQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->FOUR_STATE_LOGIC_NOTEQUAL(),
               VObjectType::paBinOp_FourStateLogicNotEqual);
  } else if (ctx->WILD_EQUAL_OP()) {
    addVObject((antlr4::ParserRuleContext *)ctx->WILD_EQUAL_OP(),
               VObjectType::paBinOp_WildEqual);
  } else if (ctx->WILD_NOTEQUAL_OP()) {
    addVObject((antlr4::ParserRuleContext *)ctx->WILD_NOTEQUAL_OP(),
               VObjectType::paBinOp_WildEqual);
  } else if (ctx->BITW_AND()) {
    if (ctx->expression().size() == 1)
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_AND(),
                 VObjectType::paUnary_BitwAnd);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_AND(),
                 VObjectType::paBinOp_BitwAnd);
  } else if (!ctx->LOGICAL_AND().empty()) {
    addVObject((antlr4::ParserRuleContext *)ctx->LOGICAL_AND()[0],
               VObjectType::paBinOp_LogicAnd);
  } else if (ctx->LOGICAL_OR()) {
    addVObject((antlr4::ParserRuleContext *)ctx->LOGICAL_OR(),
               VObjectType::paBinOp_LogicOr);
  } else if (ctx->IMPLY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->IMPLY(),
               VObjectType::paBinOp_Imply);
  } else if (ctx->EQUIVALENCE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->EQUIVALENCE(),
               VObjectType::paBinOp_Equivalence);
  } else if (ctx->TAGGED()) {
    addVObject((antlr4::ParserRuleContext *)ctx->TAGGED(), VObjectType::TAGGED);
  }
  if (ctx->QMARK()) {
    addVObject((antlr4::ParserRuleContext *)ctx->QMARK(), VObjectType::QMARK);
  }
  addVObject(ctx, VObjectType::paExpression);
}

void SV3_1aTreeShapeListener::exitEvent_expression(
    SV3_1aParser::Event_expressionContext *ctx) {
  if (ctx->IFF()) {
    addVObject((antlr4::ParserRuleContext *)ctx->IFF(), VObjectType::IFF);
  }
  addVObject(ctx, VObjectType::paEvent_expression);
}

void SV3_1aTreeShapeListener::exitConstant_expression(
    SV3_1aParser::Constant_expressionContext *ctx) {
  if (ctx->PLUS()) {
    if (ctx->constant_primary())
      addVObject((antlr4::ParserRuleContext *)ctx->PLUS(),
                 VObjectType::paUnary_Plus);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->PLUS(),
                 VObjectType::paBinOp_Plus);
  } else if (ctx->MINUS()) {
    if (ctx->constant_primary())
      addVObject((antlr4::ParserRuleContext *)ctx->MINUS(),
                 VObjectType::paUnary_Minus);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->MINUS(),
                 VObjectType::paBinOp_Minus);
  } else if (ctx->BANG()) {
    addVObject((antlr4::ParserRuleContext *)ctx->BANG(),
               VObjectType::paUnary_Not);
  } else if (ctx->TILDA()) {
    addVObject((antlr4::ParserRuleContext *)ctx->TILDA(),
               VObjectType::paUnary_Tilda);
  } else if (ctx->BITW_AND()) {
    if (ctx->constant_primary())
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_AND(),
                 VObjectType::paUnary_BitwAnd);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_AND(),
                 VObjectType::paBinOp_BitwAnd);
  } else if (ctx->BITW_OR()) {
    if (ctx->constant_primary())
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_OR(),
                 VObjectType::paUnary_BitwOr);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_OR(),
                 VObjectType::paBinOp_BitwOr);
  } else if (ctx->BITW_XOR()) {
    if (ctx->constant_primary())
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_XOR(),
                 VObjectType::paUnary_BitwXor);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_XOR(),
                 VObjectType::paBinOp_BitwXor);
  } else if (ctx->REDUCTION_NAND()) {
    if (ctx->constant_primary())
      addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_NAND(),
                 VObjectType::paUnary_ReductNand);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_NAND(),
                 VObjectType::paBinOp_ReductNand);
  } else if (ctx->REDUCTION_NOR()) {
    addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_NOR(),
               VObjectType::paUnary_ReductNor);
  } else if (ctx->REDUCTION_XNOR1()) {
    if (ctx->constant_primary())
      addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_XNOR1(),
                 VObjectType::paUnary_ReductXnor1);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_XNOR1(),
                 VObjectType::paBinOp_ReductXnor1);
  } else if (ctx->REDUCTION_XNOR2()) {
    if (ctx->constant_primary())
      addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_XNOR2(),
                 VObjectType::paUnary_ReductXnor2);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->REDUCTION_XNOR2(),
                 VObjectType::paBinOp_ReductXnor2);
  } else if (ctx->STARSTAR()) {
    addVObject((antlr4::ParserRuleContext *)ctx->STARSTAR(),
               VObjectType::paBinOp_MultMult);
  } else if (ctx->STAR()) {
    addVObject((antlr4::ParserRuleContext *)ctx->STAR(),
               VObjectType::paBinOp_Mult);
  } else if (ctx->DIV()) {
    addVObject((antlr4::ParserRuleContext *)ctx->DIV(),
               VObjectType::paBinOp_Div);
  } else if (ctx->PERCENT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->PERCENT(),
               VObjectType::paBinOp_Percent);
  } else if (ctx->SHIFT_RIGHT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SHIFT_RIGHT(),
               VObjectType::paBinOp_ShiftRight);
  } else if (ctx->SHIFT_LEFT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SHIFT_LEFT(),
               VObjectType::paBinOp_ShiftLeft);
  } else if (ctx->ARITH_SHIFT_RIGHT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->ARITH_SHIFT_RIGHT(),
               VObjectType::paBinOp_ArithShiftRight);
  } else if (ctx->ARITH_SHIFT_LEFT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->ARITH_SHIFT_LEFT(),
               VObjectType::paBinOp_ArithShiftLeft);
  } else if (ctx->LESS()) {
    addVObject((antlr4::ParserRuleContext *)ctx->LESS(),
               VObjectType::paBinOp_Less);
  } else if (ctx->LESS_EQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->LESS_EQUAL(),
               VObjectType::paBinOp_LessEqual);
  } else if (ctx->GREATER()) {
    addVObject((antlr4::ParserRuleContext *)ctx->GREATER(),
               VObjectType::paBinOp_Great);
  } else if (ctx->GREATER_EQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->GREATER_EQUAL(),
               VObjectType::paBinOp_GreatEqual);
  } else if (ctx->INSIDE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->INSIDE(), VObjectType::INSIDE);
  } else if (ctx->EQUIV()) {
    addVObject((antlr4::ParserRuleContext *)ctx->EQUIV(),
               VObjectType::paBinOp_Equiv);
  } else if (ctx->NOTEQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->NOTEQUAL(),
               VObjectType::paBinOp_Not);
  } else if (ctx->BINARY_WILDCARD_EQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->BINARY_WILDCARD_EQUAL(),
               VObjectType::paBinOp_WildcardEqual);
  } else if (ctx->BINARY_WILDCARD_NOTEQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->BINARY_WILDCARD_NOTEQUAL(),
               VObjectType::paBinOp_WildcardNotEqual);
  } else if (ctx->FOUR_STATE_LOGIC_EQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->FOUR_STATE_LOGIC_EQUAL(),
               VObjectType::paBinOp_FourStateLogicEqual);
  } else if (ctx->FOUR_STATE_LOGIC_NOTEQUAL()) {
    addVObject((antlr4::ParserRuleContext *)ctx->FOUR_STATE_LOGIC_NOTEQUAL(),
               VObjectType::paBinOp_FourStateLogicNotEqual);
  } else if (ctx->WILD_EQUAL_OP()) {
    addVObject((antlr4::ParserRuleContext *)ctx->WILD_EQUAL_OP(),
               VObjectType::paBinOp_WildEqual);
  } else if (ctx->WILD_NOTEQUAL_OP()) {
    addVObject((antlr4::ParserRuleContext *)ctx->WILD_NOTEQUAL_OP(),
               VObjectType::paBinOp_WildEqual);
  } else if (ctx->BITW_AND()) {
    if (ctx->constant_primary())
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_AND(),
                 VObjectType::paUnary_BitwAnd);
    else
      addVObject((antlr4::ParserRuleContext *)ctx->BITW_AND(),
                 VObjectType::paBinOp_BitwAnd);
  } else if (!ctx->LOGICAL_AND().empty()) {
    addVObject((antlr4::ParserRuleContext *)ctx->LOGICAL_AND()[0],
               VObjectType::paBinOp_LogicAnd);
  } else if (ctx->LOGICAL_OR()) {
    addVObject((antlr4::ParserRuleContext *)ctx->LOGICAL_OR(),
               VObjectType::paBinOp_LogicOr);
  } else if (ctx->IMPLY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->IMPLY(),
               VObjectType::paBinOp_Imply);
  } else if (ctx->EQUIVALENCE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->EQUIVALENCE(),
               VObjectType::paBinOp_Equivalence);
  }
  addVObject(ctx, VObjectType::paConstant_expression);
}

void SV3_1aTreeShapeListener::exitNet_type(SV3_1aParser::Net_typeContext *ctx) {
  if (ctx->SUPPLY0()) {
    addVObject(ctx, VObjectType::paNetType_Supply0);
  } else if (ctx->SUPPLY1()) {
    addVObject(ctx, VObjectType::paNetType_Supply1);
  } else if (ctx->TRI()) {
    addVObject(ctx, VObjectType::paNetType_Tri);
  } else if (ctx->TRIAND()) {
    addVObject(ctx, VObjectType::paNetType_TriAnd);
  } else if (ctx->TRIOR()) {
    addVObject(ctx, VObjectType::paNetType_TriOr);
  } else if (ctx->TRIREG()) {
    addVObject(ctx, VObjectType::paNetType_TriReg);
  } else if (ctx->TRI0()) {
    addVObject(ctx, VObjectType::paNetType_Tri0);
  } else if (ctx->TRI1()) {
    addVObject(ctx, VObjectType::paNetType_Tri1);
  } else if (ctx->UWIRE()) {
    addVObject(ctx, VObjectType::paNetType_Uwire);
  } else if (ctx->WIRE()) {
    addVObject(ctx, VObjectType::paNetType_Wire);
  } else if (ctx->WAND()) {
    addVObject(ctx, VObjectType::paNetType_Wand);
  } else if (ctx->WOR()) {
    addVObject(ctx, VObjectType::paNetType_Wor);
  }
}

void SV3_1aTreeShapeListener::exitAssignment_operator(
    SV3_1aParser::Assignment_operatorContext *ctx) {
  if (ctx->ASSIGN_OP()) {
    addVObject(ctx, VObjectType::paAssignOp_Assign);
  } else if (ctx->ADD_ASSIGN()) {
    addVObject(ctx, VObjectType::paAssignOp_Add);
  } else if (ctx->SUB_ASSIGN()) {
    addVObject(ctx, VObjectType::paAssignOp_Sub);
  } else if (ctx->MULT_ASSIGN()) {
    addVObject(ctx, VObjectType::paAssignOp_Mult);
  } else if (ctx->DIV_ASSIGN()) {
    addVObject(ctx, VObjectType::paAssignOp_Div);
  } else if (ctx->MODULO_ASSIGN()) {
    addVObject(ctx, VObjectType::paAssignOp_Modulo);
  } else if (ctx->BITW_AND_ASSIGN()) {
    addVObject(ctx, VObjectType::paAssignOp_BitwAnd);
  } else if (ctx->BITW_OR_ASSIGN()) {
    addVObject(ctx, VObjectType::paAssignOp_BitwOr);
  } else if (ctx->BITW_XOR_ASSIGN()) {
    addVObject(ctx, VObjectType::paAssignOp_BitwXor);
  } else if (ctx->BITW_LEFT_SHIFT_ASSIGN()) {
    addVObject(ctx, VObjectType::paAssignOp_BitwLeftShift);
  } else if (ctx->BITW_RIGHT_SHIFT_ASSIGN()) {
    addVObject(ctx, VObjectType::paAssignOp_BitwRightShift);
  } else if (ctx->ARITH_SHIFT_LEFT_ASSIGN()) {
    addVObject(ctx, VObjectType::paAssignOp_ArithShiftLeft);
  } else if (ctx->ARITH_SHIFT_RIGHT_ASSIGN()) {
    addVObject(ctx, VObjectType::paAssignOp_ArithShiftRight);
  }
}

void SV3_1aTreeShapeListener::exitInc_or_dec_operator(
    SV3_1aParser::Inc_or_dec_operatorContext *ctx) {
  if (ctx->PLUSPLUS())
    addVObject(ctx, VObjectType::paIncDec_PlusPlus);
  else
    addVObject(ctx, VObjectType::paIncDec_MinusMinus);
}

void SV3_1aTreeShapeListener::exitGate_instantiation(
    SV3_1aParser::Gate_instantiationContext *ctx) {
  if (ctx->PULLUP()) {
    addVObject((antlr4::ParserRuleContext *)ctx->PULLUP(), VObjectType::PULLUP);
  } else if (ctx->PULLDOWN()) {
    addVObject((antlr4::ParserRuleContext *)ctx->PULLDOWN(),
               VObjectType::PULLDOWN);
  }
  addVObject(ctx, VObjectType::paGate_instantiation);
}

void SV3_1aTreeShapeListener::exitOutput_symbol(
    SV3_1aParser::Output_symbolContext *ctx) {
  if (ctx->INTEGRAL_NUMBER()) {
    auto number = ctx->INTEGRAL_NUMBER();
    addVObject((antlr4::ParserRuleContext *)ctx->INTEGRAL_NUMBER(),
               number->getText(), VObjectType::INT_CONST);
  } else if (ctx->SIMPLE_IDENTIFIER()) {
    std::string ident = ctx->SIMPLE_IDENTIFIER()->getText();
    addVObject((antlr4::ParserRuleContext *)ctx->SIMPLE_IDENTIFIER(), ident,
               VObjectType::STRING_CONST);
  }
  addVObject(ctx, VObjectType::paOutput_symbol);
}

void SV3_1aTreeShapeListener::exitCycle_delay(
    SV3_1aParser::Cycle_delayContext *ctx) {
  if (ctx->INTEGRAL_NUMBER()) {
    auto number = ctx->INTEGRAL_NUMBER();
    addVObject((antlr4::ParserRuleContext *)ctx->INTEGRAL_NUMBER(),
               number->getText(), VObjectType::INT_CONST);
  }
  if (ctx->POUND_POUND_DELAY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->POUND_POUND_DELAY(),
               ctx->POUND_POUND_DELAY()->getText(),
               VObjectType::POUND_POUND_DELAY);
  }
  addVObject(ctx, VObjectType::paCycle_delay);
}

void SV3_1aTreeShapeListener::exitCycle_delay_range(
    SV3_1aParser::Cycle_delay_rangeContext *ctx) {
  if (ctx->POUND_POUND_DELAY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->POUND_POUND_DELAY(),
               ctx->POUND_POUND_DELAY()->getText(),
               VObjectType::POUND_POUND_DELAY);
  }
  if (ctx->POUNDPOUND()) {
    addVObject((antlr4::ParserRuleContext *)ctx->POUNDPOUND(),
               ctx->POUNDPOUND()->getText(), VObjectType::POUND_POUND_DELAY);
  }
  if (ctx->PLUS()) {
    addVObject((antlr4::ParserRuleContext *)ctx->PLUS(),
               VObjectType::paUnary_Plus);
  }
  if (ctx->ASSOCIATIVE_UNSPECIFIED()) {
    addVObject((antlr4::ParserRuleContext *)ctx->ASSOCIATIVE_UNSPECIFIED(),
               VObjectType::paAssociative_dimension);
  }
  addVObject(ctx, VObjectType::paCycle_delay_range);
}

void SV3_1aTreeShapeListener::exitSequence_expr(
    SV3_1aParser::Sequence_exprContext *ctx) {
  if (ctx->WITHIN()) {
    addVObject((antlr4::ParserRuleContext *)ctx->WITHIN(), VObjectType::WITHIN);
  }
  if (ctx->THROUGHOUT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->THROUGHOUT(),
               VObjectType::THROUGHOUT);
  }
  if (ctx->FIRST_MATCH()) {
    addVObject((antlr4::ParserRuleContext *)ctx->FIRST_MATCH(),
               VObjectType::FIRST_MATCH);
  }
  if (ctx->INTERSECT()) {
    addVObject((antlr4::ParserRuleContext *)ctx->INTERSECT(),
               VObjectType::INTERSECT);
  }
  if (ctx->AND()) {
    addVObject((antlr4::ParserRuleContext *)ctx->AND(), VObjectType::AND);
  }
  if (ctx->OR()) {
    addVObject((antlr4::ParserRuleContext *)ctx->OR(), VObjectType::OR);
  }
  addVObject(ctx, VObjectType::paSequence_expr);
}

void SV3_1aTreeShapeListener::exitLevel_symbol(
    SV3_1aParser::Level_symbolContext *ctx) {
  if (ctx->INTEGRAL_NUMBER()) {
    auto number = ctx->INTEGRAL_NUMBER();
    addVObject((antlr4::ParserRuleContext *)ctx->INTEGRAL_NUMBER(),
               number->getText(), VObjectType::INT_CONST);
  } else if (ctx->SIMPLE_IDENTIFIER()) {
    std::string ident = ctx->SIMPLE_IDENTIFIER()->getText();
    addVObject((antlr4::ParserRuleContext *)ctx->SIMPLE_IDENTIFIER(), ident,
               VObjectType::STRING_CONST);
  } else if (ctx->QMARK()) {
    addVObject((antlr4::ParserRuleContext *)ctx->QMARK(), VObjectType::QMARK);
  }
  addVObject(ctx, VObjectType::paLevel_symbol);
}

void SV3_1aTreeShapeListener::exitEdge_symbol(
    SV3_1aParser::Edge_symbolContext *ctx) {
  if (ctx->SIMPLE_IDENTIFIER()) {
    std::string ident = ctx->SIMPLE_IDENTIFIER()->getText();
    addVObject((antlr4::ParserRuleContext *)ctx->SIMPLE_IDENTIFIER(), ident,
               VObjectType::STRING_CONST);
  } else if (ctx->STAR()) {
    addVObject((antlr4::ParserRuleContext *)ctx->STAR(),
               VObjectType::paBinOp_Mult);
  }
  addVObject(ctx, VObjectType::paEdge_symbol);
}

void SV3_1aTreeShapeListener::enterUnconnected_drive_directive(
    SV3_1aParser::Unconnected_drive_directiveContext *ctx) {
  if (ctx->SIMPLE_IDENTIFIER()) {
    std::string text = ctx->SIMPLE_IDENTIFIER()->getText();
    logError(ErrorDefinition::PA_UNCONNECTED_DRIVE_VALUE, ctx, text);
  }
}

void SV3_1aTreeShapeListener::enterNounconnected_drive_directive(
    SV3_1aParser::Nounconnected_drive_directiveContext *ctx) {}

void SV3_1aTreeShapeListener::enterEveryRule(antlr4::ParserRuleContext *ctx) {}
void SV3_1aTreeShapeListener::exitEveryRule(antlr4::ParserRuleContext *ctx) {}
void SV3_1aTreeShapeListener::visitTerminal(antlr4::tree::TerminalNode *node) {}
void SV3_1aTreeShapeListener::visitErrorNode(antlr4::tree::ErrorNode *node) {}

void SV3_1aTreeShapeListener::exitBegin_keywords_directive(
    SV3_1aParser::Begin_keywords_directiveContext *ctx) {}

void SV3_1aTreeShapeListener::exitEnd_keywords_directive(
    SV3_1aParser::End_keywords_directiveContext *ctx) {}

void SV3_1aTreeShapeListener::exitRandomize_call(
    SV3_1aParser::Randomize_callContext *ctx) {
  if (ctx->NULL_KEYWORD()) {
    addVObject((antlr4::ParserRuleContext *)ctx->NULL_KEYWORD(),
               VObjectType::NULL_KEYWORD);
  }
  if (ctx->WITH()) {
    addVObject((antlr4::ParserRuleContext *)ctx->WITH(), VObjectType::WITH);
  }
  addVObject(ctx, VObjectType::paRandomize_call);
}

void SV3_1aTreeShapeListener::exitDeferred_immediate_assert_statement(
    SV3_1aParser::Deferred_immediate_assert_statementContext *ctx) {
  if (ctx->POUND_DELAY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->POUND_DELAY(),
               ctx->POUND_DELAY()->getText(), VObjectType::POUND_DELAY);
  } else if (ctx->POUND_POUND_DELAY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->POUND_POUND_DELAY(),
               ctx->POUND_POUND_DELAY()->getText(),
               VObjectType::POUND_POUND_DELAY);
  }
  addVObject(ctx, VObjectType::paDeferred_immediate_assert_statement);
}

void SV3_1aTreeShapeListener::exitDeferred_immediate_assume_statement(
    SV3_1aParser::Deferred_immediate_assume_statementContext *ctx) {
  if (ctx->POUND_DELAY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->POUND_DELAY(),
               ctx->POUND_DELAY()->getText(), VObjectType::POUND_DELAY);
  } else if (ctx->POUND_POUND_DELAY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->POUND_POUND_DELAY(),
               ctx->POUND_POUND_DELAY()->getText(),
               VObjectType::POUND_POUND_DELAY);
  }
  addVObject(ctx, VObjectType::paDeferred_immediate_assume_statement);
}

void SV3_1aTreeShapeListener::exitDeferred_immediate_cover_statement(
    SV3_1aParser::Deferred_immediate_cover_statementContext *ctx) {
  if (ctx->POUND_DELAY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->POUND_DELAY(),
               ctx->POUND_DELAY()->getText(), VObjectType::POUND_DELAY);
  } else if (ctx->POUND_POUND_DELAY()) {
    addVObject((antlr4::ParserRuleContext *)ctx->POUND_POUND_DELAY(),
               ctx->POUND_POUND_DELAY()->getText(),
               VObjectType::POUND_POUND_DELAY);
  }
  addVObject(ctx, VObjectType::paDeferred_immediate_cover_statement);
}

void SV3_1aTreeShapeListener::exitLocal_parameter_declaration(
    SV3_1aParser::Local_parameter_declarationContext *ctx) {
  if (ctx->TYPE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->TYPE(), VObjectType::TYPE);
  }
  addVObject(ctx, VObjectType::paLocal_parameter_declaration);
}

void SV3_1aTreeShapeListener::exitParameter_declaration(
    SV3_1aParser::Parameter_declarationContext *ctx) {
  if (ctx->TYPE()) {
    addVObject((antlr4::ParserRuleContext *)ctx->TYPE(), VObjectType::TYPE);
  }
  addVObject(ctx, VObjectType::paParameter_declaration);
}

void SV3_1aTreeShapeListener::exitPort_direction(
    SV3_1aParser::Port_directionContext *ctx) {
  if (ctx->INPUT()) {
    addVObject(ctx, VObjectType::paPortDir_Inp);
  } else if (ctx->OUTPUT()) {
    addVObject(ctx, VObjectType::paPortDir_Out);
  } else if (ctx->INOUT()) {
    addVObject(ctx, VObjectType::paPortDir_Inout);
  } else if (ctx->REF()) {
    addVObject(ctx, VObjectType::paPortDir_Ref);
  }
}

void SV3_1aTreeShapeListener::exitInteger_atom_type(
    SV3_1aParser::Integer_atom_typeContext *ctx) {
  if (ctx->INT())
    addVObject(ctx, VObjectType::paIntegerAtomType_Int);
  else if (ctx->BYTE())
    addVObject(ctx, VObjectType::paIntegerAtomType_Byte);
  else if (ctx->SHORTINT())
    addVObject(ctx, VObjectType::paIntegerAtomType_Shortint);
  else if (ctx->LONGINT())
    addVObject(ctx, VObjectType::paIntegerAtomType_LongInt);
  else if (ctx->INTEGER())
    addVObject(ctx, VObjectType::paIntegerAtomType_Integer);
  else if (ctx->TIME())
    addVObject(ctx, VObjectType::paIntegerAtomType_Time);
}

void SV3_1aTreeShapeListener::exitInteger_vector_type(
    SV3_1aParser::Integer_vector_typeContext *ctx) {
  if (ctx->LOGIC())
    addVObject(ctx, VObjectType::paIntVec_TypeLogic);
  else if (ctx->REG())
    addVObject(ctx, VObjectType::paIntVec_TypeReg);
  else if (ctx->BIT())
    addVObject(ctx, VObjectType::paIntVec_TypeBit);
}

void SV3_1aTreeShapeListener::exitNon_integer_type(
    SV3_1aParser::Non_integer_typeContext *ctx) {
  if (ctx->SHORTREAL())
    addVObject(ctx, VObjectType::paNonIntType_ShortReal);
  else if (ctx->REAL())
    addVObject(ctx, VObjectType::paNonIntType_Real);
  else if (ctx->REALTIME())
    addVObject(ctx, VObjectType::paNonIntType_RealTime);
}

void SV3_1aTreeShapeListener::exitAlways_keyword(
    SV3_1aParser::Always_keywordContext *ctx) {
  if (ctx->ALWAYS_COMB()) {
    addVObject(ctx, VObjectType::ALWAYS_COMB);
  } else if (ctx->ALWAYS_FF()) {
    addVObject(ctx, VObjectType::ALWAYS_FF);
  } else if (ctx->ALWAYS_LATCH()) {
    addVObject(ctx, VObjectType::ALWAYS_LATCH);
  } else if (ctx->ALWAYS()) {
    addVObject(ctx, VObjectType::ALWAYS);
  }
}

void SV3_1aTreeShapeListener::exitEdge_identifier(
    SV3_1aParser::Edge_identifierContext *ctx) {
  if (ctx->POSEDGE())
    addVObject(ctx, VObjectType::paEdge_Posedge);
  else if (ctx->NEGEDGE())
    addVObject(ctx, VObjectType::paEdge_Negedge);
  else if (ctx->EDGE())
    addVObject(ctx, VObjectType::paEdge_Edge);
}

void SV3_1aTreeShapeListener::exitNumber(SV3_1aParser::NumberContext *ctx) {
  if (ctx->INTEGRAL_NUMBER()) {
    auto number = ctx->INTEGRAL_NUMBER();
    addVObject(ctx, number->getText(), VObjectType::INT_CONST);
  } else if (ctx->REAL_NUMBER())
    addVObject(ctx, ctx->REAL_NUMBER()->getText(), VObjectType::REAL_CONST);
  else if (ctx->ONE_TICK_b0())
    addVObject(ctx, VObjectType::paNumber_1Tickb0);
  else if (ctx->ONE_TICK_b1())
    addVObject(ctx, VObjectType::paNumber_1Tickb1);
  else if (ctx->ONE_TICK_B0())
    addVObject(ctx, VObjectType::paNumber_1TickB0);
  else if (ctx->ONE_TICK_B1())
    addVObject(ctx, VObjectType::paNumber_1TickB1);
  else if (ctx->TICK_b0())
    addVObject(ctx, VObjectType::paNumber_Tickb0);
  else if (ctx->TICK_b1())
    addVObject(ctx, VObjectType::paNumber_Tickb1);
  else if (ctx->TICK_B0())
    addVObject(ctx, VObjectType::paNumber_TickB0);
  else if (ctx->TICK_B1())
    addVObject(ctx, VObjectType::paNumber_TickB1);
  else if (ctx->TICK_0())
    addVObject(ctx, VObjectType::paNumber_Tick0);
  else if (ctx->TICK_1())
    addVObject(ctx, VObjectType::paNumber_Tick1);
  else if (ctx->ONE_TICK_bx())
    addVObject(ctx, VObjectType::paNumber_1Tickbx);
  else if (ctx->ONE_TICK_bX())
    addVObject(ctx, VObjectType::paNumber_1TickbX);
  else if (ctx->ONE_TICK_Bx())
    addVObject(ctx, VObjectType::paNumber_1TickBx);
  else if (ctx->ONE_TICK_BX())
    addVObject(ctx, VObjectType::paNumber_1TickbX);
}

void SV3_1aTreeShapeListener::exitSigning(SV3_1aParser::SigningContext *ctx) {
  if (ctx->SIGNED())
    addVObject(ctx, VObjectType::paSigning_Signed);
  else if (ctx->UNSIGNED())
    addVObject(ctx, VObjectType::paSigning_Unsigned);
}

void SV3_1aTreeShapeListener::exitTf_port_direction(
    SV3_1aParser::Tf_port_directionContext *ctx) {
  if (ctx->INPUT())
    addVObject(ctx, VObjectType::paTfPortDir_Inp);
  else if (ctx->OUTPUT())
    addVObject(ctx, VObjectType::paTfPortDir_Out);
  else if (ctx->INOUT())
    addVObject(ctx, VObjectType::paTfPortDir_Inout);
  else if (ctx->REF())
    addVObject(ctx, VObjectType::paTfPortDir_Ref);
  else if (ctx->CONST())
    addVObject(ctx, VObjectType::paTfPortDir_ConstRef);
}

void SV3_1aTreeShapeListener::exitDefault_nettype_directive(
    SV3_1aParser::Default_nettype_directiveContext *ctx) {
  NetTypeInfo info;
  info.m_type = VObjectType::paNetType_Wire;
  info.m_fileId = m_pf->getFileId(0);
  LineColumn lineCol = ParseUtils::getLineColumn(m_tokens, ctx);
  info.m_line = lineCol.first;
  if (ctx->SIMPLE_IDENTIFIER()) {
    addVObject((antlr4::ParserRuleContext *)ctx->SIMPLE_IDENTIFIER(),
               ctx->SIMPLE_IDENTIFIER()->getText(), VObjectType::STRING_CONST);
    info.m_type = VObjectType::NO_TYPE;
  } else if (ctx->net_type()) {
    if (ctx->net_type()->SUPPLY0())
      info.m_type = VObjectType::SUPPLY0;
    else if (ctx->net_type()->SUPPLY1())
      info.m_type = VObjectType::SUPPLY1;
    else if (ctx->net_type()->WIRE())
      info.m_type = VObjectType::paNetType_Wire;
    else if (ctx->net_type()->UWIRE())
      info.m_type = VObjectType::paNetType_Uwire;
    else if (ctx->net_type()->WAND())
      info.m_type = VObjectType::paNetType_Wand;
    else if (ctx->net_type()->WOR())
      info.m_type = VObjectType::paNetType_Wor;
    else if (ctx->net_type()->TRI())
      info.m_type = VObjectType::paNetType_Tri;
    else if (ctx->net_type()->TRIREG())
      info.m_type = VObjectType::paNetType_TriReg;
    else if (ctx->net_type()->TRIOR())
      info.m_type = VObjectType::paNetType_TriOr;
    else if (ctx->net_type()->TRIAND())
      info.m_type = VObjectType::paNetType_TriAnd;
    else if (ctx->net_type()->TRI0())
      info.m_type = VObjectType::paNetType_Tri0;
    else if (ctx->net_type()->TRI1())
      info.m_type = VObjectType::paNetType_Tri1;
  }
  addVObject(ctx, VObjectType::paDefault_nettype_directive);
  m_pf->getCompilationUnit()->recordDefaultNetType(info);
}

void SV3_1aTreeShapeListener::exitParameter_value_assignment(
    SV3_1aParser::Parameter_value_assignmentContext *ctx) {
  if (ctx->POUND_DELAY()) {
    addVObject(ctx, ctx->POUND_DELAY()->getText(), VObjectType::INT_CONST);
  }
  addVObject(ctx, VObjectType::paParameter_value_assignment);
}

void SV3_1aTreeShapeListener::exitElaboration_system_task(
    SV3_1aParser::Elaboration_system_taskContext *ctx) {
  if (ctx->number()) {
    addVObject((antlr4::ParserRuleContext *)ctx->number(),
               ctx->number()->getText(), VObjectType::INT_CONST);
  }
  addVObject((antlr4::ParserRuleContext *)ctx->SIMPLE_IDENTIFIER(),
             ctx->SIMPLE_IDENTIFIER()->getText(), VObjectType::STRING_CONST);
  addVObject(ctx, VObjectType::paElaboration_system_task);
}

void SV3_1aTreeShapeListener::exitSimple_type(
    SV3_1aParser::Simple_typeContext *ctx) {
  addVObject(ctx, ctx->getText(), VObjectType::paSimple_type);
}

void SV3_1aTreeShapeListener::exitString_type(
    SV3_1aParser::String_typeContext *ctx) {
  addVObject(ctx, ctx->getText(), VObjectType::paString_type);
}

void SV3_1aTreeShapeListener::exitThis_dot_super(
    SV3_1aParser::This_dot_superContext *ctx) {
  addVObject(ctx, ctx->getText(), VObjectType::paThis_dot_super);
}

void SV3_1aTreeShapeListener::exitImplicit_class_handle(
    SV3_1aParser::Implicit_class_handleContext *ctx) {
  addVObject(ctx, ctx->getText(), VObjectType::paImplicit_class_handle);
}
}  // namespace SURELOG
