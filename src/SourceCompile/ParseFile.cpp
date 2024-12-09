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
 * File:   ParseFile.cpp
 * Author: alain
 *
 * Created on February 24, 2017, 10:03 PM
 */

#include <Surelog/Cache/ParseCache.h>
#include <Surelog/CommandLine/CommandLineParser.h>
#include <Surelog/Common/FileSystem.h>
#include <Surelog/Common/Session.h>
#include <Surelog/Design/FileContent.h>
#include <Surelog/ErrorReporting/ErrorContainer.h>
#include <Surelog/Package/Precompiled.h>
#include <Surelog/SourceCompile/AntlrParserErrorListener.h>
#include <Surelog/SourceCompile/AntlrParserHandler.h>
#include <Surelog/SourceCompile/CompileSourceFile.h>
#include <Surelog/SourceCompile/MacroInfo.h>
#include <Surelog/SourceCompile/ParseFile.h>
#include <Surelog/SourceCompile/SV3_1aParseTreeListener.h>
#include <Surelog/SourceCompile/SV3_1aTreeShapeListener.h>
#include <Surelog/SourceCompile/SymbolTable.h>
#include <Surelog/Utils/StringUtils.h>
#include <Surelog/Utils/Timer.h>
#include <antlr4-runtime.h>
#include <parser/SV3_1aLexer.h>
#include <parser/SV3_1aParser.h>

namespace SURELOG {
ParseFile::ParseFile(Session* session, PathId fileId)
    : m_session(session),
      m_fileId(fileId),
      m_compileSourceFile(nullptr),
      m_compilationUnit(nullptr),
      m_library(nullptr),
      m_antlrParserHandler(nullptr),
      m_listener(nullptr),
      m_usingCachedVersion(false),
      m_keepParserHandler(false),
      m_fileContent(nullptr),
      debug_AstModel(false),
      m_parent(nullptr),
      m_offsetLine(0) {
  debug_AstModel = false;
}

ParseFile::ParseFile(Session* session, PathId fileId, CompileSourceFile* csf,
                     CompilationUnit* compilationUnit, Library* library,
                     PathId ppFileId, bool keepParserHandler)
    : m_session(session),
      m_fileId(fileId),
      m_ppFileId(ppFileId),
      m_compileSourceFile(csf),
      m_compilationUnit(compilationUnit),
      m_library(library),
      m_antlrParserHandler(nullptr),
      m_listener(nullptr),
      m_usingCachedVersion(false),
      m_keepParserHandler(keepParserHandler),
      m_fileContent(nullptr),
      debug_AstModel(false),
      m_parent(nullptr),
      m_offsetLine(0) {
  debug_AstModel = m_session->getCommandLineParser()->getDebugAstModel();
}

ParseFile::ParseFile(Session* session, CompileSourceFile* compileSourceFile,
                     ParseFile* parent, PathId chunkFileId, uint32_t offsetLine)
    : m_session(session),
      m_fileId(parent->m_fileId),
      m_ppFileId(chunkFileId),
      m_compileSourceFile(compileSourceFile),
      m_compilationUnit(parent->m_compilationUnit),
      m_library(parent->m_library),
      m_antlrParserHandler(nullptr),
      m_listener(nullptr),
      m_usingCachedVersion(false),
      m_keepParserHandler(parent->m_keepParserHandler),
      m_fileContent(parent->m_fileContent),
      debug_AstModel(false),
      m_parent(parent),
      m_offsetLine(offsetLine) {
  parent->m_children.push_back(this);
}

ParseFile::ParseFile(Session* session, std::string_view text,
                     CompileSourceFile* csf, CompilationUnit* compilationUnit,
                     Library* library)
    : m_session(session),
      m_compileSourceFile(csf),
      m_compilationUnit(compilationUnit),
      m_library(library),
      m_antlrParserHandler(nullptr),
      m_listener(nullptr),
      m_usingCachedVersion(false),
      m_keepParserHandler(false),
      m_fileContent(nullptr),
      debug_AstModel(false),
      m_parent(nullptr),
      m_offsetLine(0),
      m_sourceText(text) {
  debug_AstModel = m_session->getCommandLineParser()->getDebugAstModel();
}

ParseFile::~ParseFile() {
  if (!m_keepParserHandler) delete m_antlrParserHandler;
  delete m_listener;
}

SymbolId ParseFile::registerSymbol(std::string_view symbol) {
  return m_session->getSymbolTable()->registerSymbol(symbol);
}

SymbolId ParseFile::getId(std::string_view symbol) const {
  return m_session->getSymbolTable()->getId(symbol);
}

std::string_view ParseFile::getSymbol(SymbolId id) const {
  return m_session->getSymbolTable()->getSymbol(id);
}

void ParseFile::addError(Error& error) {
  m_session->getErrorContainer()->addError(error);
}

PathId ParseFile::getFileId(uint32_t line) {
  if ((line == 0) || !getCompileSourceFile()) return m_fileId;

  PreprocessFile* pp = getCompileSourceFile()->getPreprocessor();
  if (!pp) return BadPathId;

  const auto& infos = pp->getIncludeFileInfo();
  if (infos.empty()) return m_fileId;

  if (m_locationCache.empty()) buildLocationCache();

  if (line < m_locationCache.size()) {
    return std::get<1>(m_locationCache[line][0]);
  }

  SymbolId fileId = registerSymbol("FILE CACHE OUT OF BOUND");
  Location ppfile(fileId);
  Error err(ErrorDefinition::PA_INTERNAL_WARNING, ppfile);
  addError(err);

  return m_fileId;
}

uint32_t ParseFile::getLineNb(uint32_t line) {
  if (!getCompileSourceFile()) return line;

  PreprocessFile* pp = getCompileSourceFile()->getPreprocessor();
  if (!pp) return 0;

  auto& infos = pp->getIncludeFileInfo();
  if (infos.empty()) return line;

  if (m_locationCache.empty()) buildLocationCache();

  if (line < m_locationCache.size()) {
    return std::get<2>(m_locationCache[line][0]);
  }

  SymbolId fileId = registerSymbol("LINE CACHE OUT OF BOUND");
  Location ppfile(fileId);
  Error err(ErrorDefinition::PA_INTERNAL_WARNING, ppfile);
  addError(err);

  return line;
}

void ParseFile::printLocationCache() const {
#ifdef _WIN32
  FileSystem* const fileSystem = m_session->getFileSystem();
  // std::string filepath =
  //    fileSystem->toPlatformAbsPath(m_fileId).string() + ".log";
  // std::ofstream strm(filepath);
  std::ostream& strm = std::cout;

  uint32_t index = 0;
  strm << "File: " << PathIdPP(m_fileId, fileSystem) << std::endl;
  for (const auto& entry1 : m_locationCache) {
    for (const auto& entry2 : entry1) {
      strm << index << ": " << std::get<0>(entry2) << ", "
           << PathIdPP(std::get<1>(entry2), fileSystem) << ", "
           << std::get<2>(entry2) << ", " << std::get<3>(entry2) << ", "
           << std::get<4>(entry2) << std::endl;
    }
    strm << std::endl;
    ++index;
  }
  strm << std::endl << std::endl;
  strm << std::flush;
  // strm.close();
#endif
}

void ParseFile::buildLocationCache_recurse(uint32_t index,
                                           const token_offsets_t& offsets) {
  auto const& infos =
      getCompileSourceFile()->getPreprocessor()->getIncludeFileInfo();
  const IncludeFileInfo& oifi = infos[index];
  const IncludeFileInfo& cifi = infos[oifi.m_indexOpposite];

  uint32_t sourceLine = oifi.m_sourceLine;
  uint32_t targetLine = 1;
  PathId targetFileId;

  if (oifi.m_context == IncludeFileInfo::Context::INCLUDE) {
    targetFileId = oifi.m_sectionFileId;
    while (sourceLine < oifi.m_sourceLine) {
      m_locationCache[sourceLine++].emplace_back(1, targetFileId, targetLine++,
                                                 0, index);
    }
    m_locationCache[sourceLine].emplace_back(1, targetFileId, targetLine, 0,
                                             index);
    if (oifi.m_sourceColumn > 1) {
      m_locationCache[sourceLine].emplace_back(
          oifi.m_sourceColumn, targetFileId, targetLine, 0, index);
    }
  } else if (oifi.m_context == IncludeFileInfo::Context::MACRO) {
    targetLine = oifi.m_macroDefinition->m_startLine;
    targetFileId = oifi.m_macroDefinition->m_fileId;
    m_locationCache[sourceLine].emplace_back(oifi.m_sourceColumn, targetFileId,
                                             targetLine,
                                             oifi.m_sourceColumn - 1, index);
  }

  int32_t pio = -1;
  for (int32_t i = index + 1; i < oifi.m_indexOpposite; ++i) {
    const IncludeFileInfo& ioifi = infos[i];
    if (ioifi.m_action != IncludeFileInfo::Action::PUSH) continue;

    const IncludeFileInfo& icifi = infos[ioifi.m_indexOpposite];

    if (pio > 0) {
      const IncludeFileInfo& pioifi = infos[pio];
      const IncludeFileInfo& picifi = infos[pioifi.m_indexOpposite];

      if (pioifi.m_context == IncludeFileInfo::Context::MACRO) {
        for (const auto& [line, column, offset] : offsets[index]) {
          if ((line == targetLine) && (column >= picifi.m_sourceColumn)) {
            m_locationCache[sourceLine].emplace_back(column, targetFileId,
                                                     targetLine, offset, index);
          } else if (line > targetLine) {
            break;
          }
        }
      }
    }

    while (sourceLine < ioifi.m_sourceLine) {
      m_locationCache[++sourceLine].emplace_back(1, targetFileId, ++targetLine,
                                                 0, index);
    }

    buildLocationCache_recurse(i, offsets);

    targetLine += (icifi.m_symbolLine - ioifi.m_symbolLine);

    if (ioifi.m_context == IncludeFileInfo::Context::INCLUDE) {
      if (icifi.m_sourceColumn > 1) {
        // Included file doesn't have newline at the end of the file!
        m_locationCache[sourceLine].emplace_back(
            icifi.m_sourceColumn, targetFileId, targetLine,
            icifi.m_sourceColumn - icifi.m_symbolColumn, index);
      }
    } else if (ioifi.m_context == IncludeFileInfo::Context::MACRO) {
      uint16_t symbolEndColumn = icifi.m_symbolColumn;
      for (const auto& [line, column, offset] : offsets[index]) {
        if ((line == targetLine) && (icifi.m_symbolColumn >= column)) {
          symbolEndColumn = icifi.m_symbolColumn - offset;
        } else if (line > targetLine) {
          break;
        }
      }

      m_locationCache[icifi.m_sourceLine].emplace_back(
          icifi.m_sourceColumn, targetFileId, targetLine,
          icifi.m_sourceColumn - symbolEndColumn, index);
    }

    pio = i;
    i = ioifi.m_indexOpposite;
    sourceLine = icifi.m_sourceLine;
  }

  if ((oifi.m_context == IncludeFileInfo::Context::INCLUDE) && (pio > 0)) {
    const IncludeFileInfo& pioifi = infos[pio];
    const IncludeFileInfo& picifi = infos[pioifi.m_indexOpposite];

    if ((picifi.m_context == IncludeFileInfo::Context::INCLUDE) &&
        (picifi.m_sourceColumn > 1)) {
      // Last included file doesn't have a newline at the end of the file!
      m_locationCache[sourceLine].emplace_back(
          picifi.m_sourceColumn, targetFileId, targetLine - 1,
          picifi.m_sourceColumn - picifi.m_symbolColumn, index);
    }

    if (picifi.m_sourceLine < cifi.m_sourceLine) {
      ++sourceLine;
      ++targetLine;
    }
  }

  while (sourceLine <= cifi.m_sourceLine) {
    if (m_locationCache[sourceLine].empty()) {
      m_locationCache[sourceLine].emplace_back(1, targetFileId, targetLine, 0,
                                               index);
    }
    ++sourceLine;
    ++targetLine;
  }
}

void ParseFile::buildLocationCache() {
  PreprocessFile* pp = getCompileSourceFile()->getPreprocessor();
  if (!pp) return;

  auto& infos = pp->getIncludeFileInfo();
  if (infos.empty()) return;

  token_offsets_t tokenOffsets;
  tokenOffsets.reserve(infos.size());
  for (const IncludeFileInfo& info : infos) {
    token_offsets_t::value_type& offsets = tokenOffsets.emplace_back();
    if (info.m_macroDefinition == nullptr) continue;

    const uint32_t macroStartLine = info.m_macroDefinition->m_startLine;
    const uint32_t macroStartColumn = info.m_macroDefinition->m_bodyStartColumn;
    const std::vector<LineColumn>& targetPositions =
        info.m_macroDefinition->m_tokenPositions;
    const std::vector<LineColumn>& sourcePositions = info.m_tokenPositions;

    int32_t delta = 0;
    uint32_t prevSourceLine = 0;
    for (uint32_t i = 0, ni = info.m_tokenPositions.size(); i < ni; ++i) {
      if (prevSourceLine != sourcePositions[i].first) {
        prevSourceLine = sourcePositions[i].first;
        delta = 0;
      }
      const uint16_t targetColumn =
          (sourcePositions[i].first == macroStartLine)
              ? targetPositions[i].second - macroStartColumn + 1
              : targetPositions[i].second;
      if ((sourcePositions[i].second - delta) != targetColumn) {
        delta = sourcePositions[i].second - targetColumn;
        if (!offsets.empty() &&
            (std::get<0>(offsets.back()) == sourcePositions[i].first) &&
            (std::get<1>(offsets.back()) == sourcePositions[i].second)) {
          std::get<2>(offsets.back()) = delta;
        } else {
          offsets.emplace_back(sourcePositions[i].first,
                               sourcePositions[i].second, delta);
        }
      }
    }
  }

  m_locationCache.clear();
  m_locationCache.resize(pp->getLineCount() + 10);
  m_locationCache[0].emplace_back(0, m_fileId, 0, 0, -1);
  buildLocationCache_recurse(0, tokenOffsets);

  uint32_t sourceLine = infos.back().m_sourceLine + 1;
  uint32_t targetLine = std::get<2>(m_locationCache[sourceLine - 1].back()) + 1;
  while (sourceLine < m_locationCache.size()) {
    m_locationCache[sourceLine++].emplace_back(1, m_fileId, targetLine++, 0, 0);
  }
  // printLocationCache();
}

std::tuple<PathId, uint32_t, uint16_t, PathId, uint32_t, uint16_t>
ParseFile::mapLocations(uint32_t sl, uint16_t sc, uint32_t el, uint16_t ec) {
  if (!getCompileSourceFile()) return {m_fileId, sl, sc, m_fileId, el, ec};

  PreprocessFile* pp = getCompileSourceFile()->getPreprocessor();
  if (!pp) return {m_fileId, sl, sc, m_fileId, el, ec};

  const auto& infos = pp->getIncludeFileInfo();
  if (infos.empty()) return {m_fileId, sl, sc, m_fileId, el, ec};

  if (m_locationCache.empty()) buildLocationCache();
  if (m_locationCache.empty()) return {m_fileId, sl, sc, m_fileId, el, ec};

  const location_cache_entry_t& entry_s = m_locationCache[sl];
  const location_cache_entry_t& entry_e = m_locationCache[el];

  int32_t si = -1;
  if ((sl >= 1) && (sl < m_locationCache.size())) {
    for (uint32_t i = 0, ni = entry_s.size(); i < ni; ++i) {
      const location_cache_entry_t::value_type& item = entry_s[i];

      if (sc >= std::get<0>(item)) {
        si = i;
      }
    }
  }

  int32_t ei = -1;
  if ((el >= 1) && (el < m_locationCache.size())) {
    for (uint32_t i = 0, ni = entry_e.size(); i < ni; ++i) {
      const location_cache_entry_t::value_type& item = entry_e[i];

      if (ec >= std::get<0>(item)) {
        ei = i;
      }
    }
  }

  if ((si >= 0) && (ei >= 0)) {
    const location_cache_entry_t::value_type& item_s = entry_s[si];
    const location_cache_entry_t::value_type& item_e = entry_e[ei];

    const int32_t hint_s = std::get<4>(item_s);
    const int32_t hint_e = std::get<4>(item_e);

    if (hint_s != hint_e) {
      const IncludeFileInfo& info_s = infos[hint_s];
      const IncludeFileInfo& info_e = infos[hint_e];

      if ((info_s.m_context == IncludeFileInfo::Context::MACRO) &&
          (info_e.m_context == IncludeFileInfo::Context::MACRO)) {
        if ((ei > 0) && (std::get<4>(entry_e[ei - 1]) == hint_s) &&
            (ec == infos[info_s.m_indexOpposite].m_sourceColumn)) {
          --ei;
        } else if ((si > 0) && (std::get<4>(entry_s[si - 1]) == hint_e) &&
                   (sc == info_s.m_sourceColumn)) {
          --si;
        }
      } else if (info_s.m_context == IncludeFileInfo::Context::MACRO) {
        if ((ei > 0) && (std::get<4>(entry_e[ei - 1]) == hint_s) &&
            (ec == infos[info_s.m_indexOpposite].m_sourceColumn)) {
          --ei;
        } else if ((si > 0) && (std::get<4>(entry_s[si - 1]) == hint_e) &&
                   (sc == info_s.m_sourceColumn)) {
          --si;
        }
      } else if (info_e.m_context != IncludeFileInfo::Context::MACRO) {
        if ((si > 0) && (std::get<4>(entry_s[si - 1]) == hint_e)) {
          --si;
        }
      }
    }
  }

  PathId csf = m_fileId;
  uint32_t csl = sl;
  uint16_t csc = sc;
  if (si >= 0) {
    const location_cache_entry_t::value_type& item = entry_s[si];
    csf = std::get<1>(item);
    csl = std::get<2>(item);
    csc = sc - std::get<3>(item);
  }

  PathId cef = m_fileId;
  uint32_t cel = el;
  uint16_t cec = ec;
  if (ei >= 0) {
    const location_cache_entry_t::value_type& item = entry_e[ei];
    cef = std::get<1>(item);
    cel = std::get<2>(item);
    cec = ec - std::get<3>(item);
  }

  return {csf, csl, csc, cef, cel, cec};
}

bool ParseFile::parseOneFile_(PathId fileId, uint32_t lineOffset) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  CommandLineParser* const clp = m_session->getCommandLineParser();
  PreprocessFile* pp = getCompileSourceFile()->getPreprocessor();
  Timer tmr;
  m_antlrParserHandler = new AntlrParserHandler();
  m_antlrParserHandler->m_clearAntlrCache = clp->lowMem();
  if (m_sourceText.empty()) {
    std::istream& stream = fileSystem->openForRead(fileId);
    if (!stream.good()) {
      fileSystem->close(stream);
      Location ppfile(fileId);
      Error err(ErrorDefinition::PA_CANNOT_OPEN_FILE, ppfile);
      addError(err);
      return false;
    }
    m_antlrParserHandler->m_inputStream = new antlr4::ANTLRInputStream(stream);
    fileSystem->close(stream);
  } else {
    m_antlrParserHandler->m_inputStream =
        new antlr4::ANTLRInputStream(m_sourceText);
  }

  m_antlrParserHandler->m_errorListener =
      new AntlrParserErrorListener(m_session, this, false, lineOffset, fileId);
  m_antlrParserHandler->m_lexer =
      new SV3_1aLexer(m_antlrParserHandler->m_inputStream);
  VerilogVersion version = VerilogVersion::SystemVerilog;
  if (pp) version = pp->getVerilogVersion();
  if (version != VerilogVersion::NoVersion) {
    switch (version) {
      case VerilogVersion::NoVersion:
        break;
      case VerilogVersion::Verilog1995:
        m_antlrParserHandler->m_lexer->sverilog = false;
        break;
      case VerilogVersion::Verilog2001:
        m_antlrParserHandler->m_lexer->sverilog = false;
        break;
      case VerilogVersion::Verilog2005:
        m_antlrParserHandler->m_lexer->sverilog = false;
        break;
      case VerilogVersion::SVerilog2005:
        m_antlrParserHandler->m_lexer->sverilog = true;
        break;
      case VerilogVersion::Verilog2009:
        m_antlrParserHandler->m_lexer->sverilog = true;
        break;
      case VerilogVersion::SystemVerilog:
        m_antlrParserHandler->m_lexer->sverilog = true;
        break;
    }
  } else {
    std::string_view type = std::get<1>(fileSystem->getType(fileId, symbols));
    m_antlrParserHandler->m_lexer->sverilog =
        (type == ".sv") || clp->fullSVMode() || clp->isSVFile(fileId);
  }

  m_antlrParserHandler->m_lexer->removeErrorListeners();
  m_antlrParserHandler->m_lexer->addErrorListener(
      m_antlrParserHandler->m_errorListener);
  m_antlrParserHandler->m_tokens =
      new antlr4::CommonTokenStream(m_antlrParserHandler->m_lexer);
  m_antlrParserHandler->m_tokens->fill();

  if (clp->profile()) {
    // m_profileInfo += "Tokenizer: " + std::to_string (tmr.elapsed_rounded
    // ())
    // + " " + fileName + "\n";
    tmr.reset();
  }

  // Simulator options showed up in Antlr when also ANTLRCPP_VERSION was
  // first defined, so testing with ifdef helps us to decide to use options.
#ifdef ANTLRCPP_VERSION
  antlr4::atn::ParserATNSimulatorOptions options;
  options.setPredictionContextMergeCacheOptions(
      antlr4::atn::PredictionContextMergeCacheOptions()
          .setMaxSize(10000)
          .setClearEveryN(100));

  m_antlrParserHandler->m_parser =
      new SV3_1aParser(m_antlrParserHandler->m_tokens, options);
#else
  m_antlrParserHandler->m_parser =
      new SV3_1aParser(m_antlrParserHandler->m_tokens);
#endif

  if (clp->profile()) {
    m_antlrParserHandler->m_parser->setProfile(true);
  }
  m_antlrParserHandler->m_parser
      ->getInterpreter<antlr4::atn::ParserATNSimulator>()
      ->setPredictionMode(antlr4::atn::PredictionMode::SLL);
  m_antlrParserHandler->m_parser->removeErrorListeners();
  m_antlrParserHandler->m_parser->setErrorHandler(
      std::make_shared<antlr4::BailErrorStrategy>());

  try {
    m_antlrParserHandler->m_tree =
        m_antlrParserHandler->m_parser->top_level_rule();

    if (clp->profile()) {
      StrAppend(&m_profileInfo,
                "SLL Parsing: ", StringUtils::to_string(tmr.elapsed_rounded()),
                "s ", fileSystem->toPath(fileId), "\n");
      tmr.reset();
      profileParser();
    }
  } catch (antlr4::ParseCancellationException&) {
    m_antlrParserHandler->m_tokens->reset();
    m_antlrParserHandler->m_parser->reset();
    m_antlrParserHandler->m_parser->removeErrorListeners();
    if (clp->profile()) {
      m_antlrParserHandler->m_parser->setProfile(true);
    }
    m_antlrParserHandler->m_parser->setErrorHandler(
        std::make_shared<antlr4::DefaultErrorStrategy>());
    m_antlrParserHandler->m_parser->addErrorListener(
        m_antlrParserHandler->m_errorListener);
    m_antlrParserHandler->m_parser
        ->getInterpreter<antlr4::atn::ParserATNSimulator>()
        ->setPredictionMode(antlr4::atn::PredictionMode::LL);
    m_antlrParserHandler->m_tree =
        m_antlrParserHandler->m_parser->top_level_rule();

    if (clp->profile()) {
      StrAppend(&m_profileInfo,
                "LL  Parsing: ", StringUtils::to_string(tmr.elapsed_rounded()),
                "s ", fileSystem->toPath(fileId), "\n");
      tmr.reset();
      profileParser();
    }
  }
  /* Failed attempt to minimize memory usage:
     m_antlrParserHandler->m_parser->getInterpreter<antlr4::atn::ParserATNSimulator>()->clearDFA();
     SV3_1aParser::_sharedContextCache.clear();
  */
  return true;
}

void ParseFile::profileParser() {
  // Core dumps
  /*
  for (auto iterator =
  m_antlrParserHandler->m_parser->getParseInfo().getDecisionInfo().begin();
       iterator !=
  m_antlrParserHandler->m_parser->getParseInfo().getDecisionInfo().end();
  iterator++) { antlr4::atn::DecisionInfo& decisionInfo = *iterator;
    antlr4::atn::DecisionState* ds =
        m_antlrParserHandler->m_parser->getATN().getDecisionState(decisionInfo.decision);
    std::string rule =
  m_antlrParserHandler->m_parser->getRuleNames()[ds->ruleIndex]; if
  (decisionInfo.timeInPrediction > 0) { std::cout << std::left << std::setw(35)
  << std::setfill(' ') << rule; std::cout << std::left << std::setw(15) <<
  std::setfill(' ')
                << decisionInfo.timeInPrediction;
      std::cout << std::left << std::setw(15) << std::setfill(' ')
                << decisionInfo.invocations;
      std::cout << std::left << std::setw(15) << std::setfill(' ')
                << decisionInfo.SLL_TotalLook;
      std::cout << std::left << std::setw(15) << std::setfill(' ')
                << decisionInfo.SLL_MaxLook;
      std::cout << std::left << std::setw(15) << std::setfill(' ')
                << decisionInfo.ambiguities.size();
      std::cout << std::left << std::setw(15) << std::setfill(' ')
                << decisionInfo.errors.size();
      std::cout << std::endl;
    }
  }
  */
}

std::string ParseFile::getProfileInfo() const {
  std::string profile;
  profile = m_profileInfo;
  for (const ParseFile* child : m_children) {
    profile += child->m_profileInfo;
  }
  return profile;
}

bool ParseFile::parse() {
  FileSystem* const fileSystem = m_session->getFileSystem();
  CommandLineParser* const clp = m_session->getCommandLineParser();
  Precompiled* const precompiled = m_session->getPrecompiled();
  const bool isPrecompiled = precompiled->isFilePrecompiled(m_ppFileId);

  if (m_children.empty()) {
    ParseCache cache(m_session, this);

    if (cache.restore()) {
      m_usingCachedVersion = true;
      if (debug_AstModel && !isPrecompiled && m_fileId)
        m_fileContent->printTree(std::cout);
      if (clp->debugCache()) {
        std::cout << "PARSER CACHE USED FOR: "
                  << fileSystem->toPath(getFileId(0)) << std::endl;
      }
      return true;
    }
  } else {
    bool ok = true;
    for (ParseFile* child : m_children) {
      ParseCache cache(m_session, child);

      if (cache.restore()) {
        child->m_fileContent->setParent(m_fileContent);
        m_usingCachedVersion = true;
        if (debug_AstModel && !isPrecompiled && m_fileId)
          child->m_fileContent->printTree(std::cout);
      } else {
        ok = false;
      }
    }
    if (ok) {
      if (clp->debugCache()) {
        std::cout << "PARSER CACHE USED FOR: "
                  << fileSystem->toPath(getFileId(0)) << std::endl;
      }
      return true;
    }
  }

  // This is not a parent Parser object
  if (m_children.empty()) {
    // std::cout << std::endl << "Parsing " << getSymbol(m_ppFileId) << "
    // Line: " << m_offsetLine << std::endl << std::flush;

    parseOneFile_(m_ppFileId, m_offsetLine);

    // m_listener = new SV3_1aTreeShapeListener (this,
    // m_antlrParserHandler->m_tokens, m_offsetLine);
    // tree::ParseTreeWalker::DEFAULT.walk (m_listener,
    // m_antlrParserHandler->m_tree); std::cout << std::endl << "End Parsing "
    // << getSymbol(m_ppFileId) << " Line: " << m_offsetLine << std::endl <<
    // std::flush;
  }

  // This is either a parent Parser object of this Parser object has no parent
  if (!m_children.empty() || (m_parent == nullptr)) {
    if ((m_parent == nullptr) && (m_children.empty())) {
      Timer tmr;

      if (clp->parseTree()) {
        FileContent* const ppFileContent =
            getCompileSourceFile()->getPreprocessor()->getFileContent();
        m_listener = new SV3_1aParseTreeListener(m_session, this,
                                                 m_antlrParserHandler->m_tokens,
                                                 m_offsetLine, ppFileContent);
      } else {
        m_listener = new SV3_1aTreeShapeListener(
            m_session, this, m_antlrParserHandler->m_tokens, m_offsetLine);
      }

      antlr4::tree::ParseTreeWalker::DEFAULT.walk(m_listener,
                                                  m_antlrParserHandler->m_tree);

      if (debug_AstModel && !isPrecompiled && m_fileId)
        m_fileContent->printTree(std::cout);

      if (clp->profile()) {
        // m_profileInfo += "AST Walking: " + std::to_string
        // (tmr.elapsed_rounded ()) + "\n";
        tmr.reset();
      }

      ParseCache cache(m_session, this);
      if (clp->link()) return true;
      if (!cache.save()) {
        return false;
      }

      if (clp->profile()) {
        m_profileInfo +=
            "Cache saving: " + std::to_string(tmr.elapsed_rounded()) + "s\n";
        std::cout << "Cache saving: " + std::to_string(tmr.elapsed_rounded()) +
                         "s\n"
                  << std::flush;
        tmr.reset();
      }
    }

    if (!m_children.empty()) {
      for (ParseFile* child : m_children) {
        if (child->m_antlrParserHandler) {
          // Only visit the chunks that got re-parsed
          // TODO: Incrementally regenerate the FileContent
          child->m_fileContent->setParent(m_fileContent);
          if (clp->parseTree()) {
            FileContent* const ppFileContent = child->getCompileSourceFile()
                                                   ->getPreprocessor()
                                                   ->getFileContent();
            child->m_listener = new SV3_1aParseTreeListener(
                m_session, child, child->m_antlrParserHandler->m_tokens,
                child->m_offsetLine, ppFileContent);
          } else {
            child->m_listener = new SV3_1aTreeShapeListener(
                m_session, child, child->m_antlrParserHandler->m_tokens,
                child->m_offsetLine);
          }

          Timer tmr;
          antlr4::tree::ParseTreeWalker::DEFAULT.walk(
              child->m_listener, child->m_antlrParserHandler->m_tree);

          if (clp->profile()) {
            // m_profileInfo += "For file " + getSymbol
            // (child->m_ppFileId) + ", AST Walking took" +
            // std::to_string (tmr.elapsed_rounded ()) + "\n";
            tmr.reset();
          }

          if (debug_AstModel && !isPrecompiled && m_fileId)
            child->m_fileContent->printTree(std::cout);

          ParseCache cache(m_session, child);
          if (clp->link()) return true;
          if (!cache.save()) {
            return false;
          }
        }
      }
    }
  }
  return true;
}
}  // namespace SURELOG
