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
#include <Surelog/Design/FileContent.h>
#include <Surelog/ErrorReporting/ErrorContainer.h>
#include <Surelog/Package/Precompiled.h>
#include <Surelog/SourceCompile/AntlrParserErrorListener.h>
#include <Surelog/SourceCompile/AntlrParserHandler.h>
#include <Surelog/SourceCompile/CompileSourceFile.h>
#include <Surelog/SourceCompile/MacroInfo.h>
#include <Surelog/SourceCompile/ParseFile.h>
#include <Surelog/SourceCompile/SV3_1aTreeShapeListener.h>
#include <Surelog/SourceCompile/SymbolTable.h>
#include <Surelog/Utils/StringUtils.h>
#include <Surelog/Utils/Timer.h>
#include <antlr4-runtime.h>
#include <parser/SV3_1aLexer.h>
#include <parser/SV3_1aParser.h>

namespace SURELOG {
ParseFile::ParseFile(PathId fileId, SymbolTable* symbolTable,
                     ErrorContainer* errors)
    : m_fileId(fileId),
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
      m_offsetLine(0),
      m_symbolTable(symbolTable),
      m_errors(errors) {
  debug_AstModel = false;
}

ParseFile::ParseFile(PathId fileId, CompileSourceFile* csf,
                     CompilationUnit* compilationUnit, Library* library,
                     PathId ppFileId, bool keepParserHandler)
    : m_fileId(fileId),
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
      m_offsetLine(0),
      m_symbolTable(nullptr),
      m_errors(nullptr) {
  debug_AstModel =
      m_compileSourceFile->getCommandLineParser()->getDebugAstModel();
}

ParseFile::ParseFile(CompileSourceFile* compileSourceFile, ParseFile* parent,
                     PathId chunkFileId, uint32_t offsetLine)
    : m_fileId(parent->m_fileId),
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
      m_offsetLine(offsetLine),
      m_symbolTable(nullptr),
      m_errors(nullptr) {
  parent->m_children.push_back(this);
}

ParseFile::ParseFile(std::string_view text, CompileSourceFile* csf,
                     CompilationUnit* compilationUnit, Library* library)
    : m_compileSourceFile(csf),
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
      m_symbolTable(csf->getSymbolTable()),
      m_errors(csf->getErrorContainer()),
      m_sourceText(text) {
  debug_AstModel =
      m_compileSourceFile->getCommandLineParser()->getDebugAstModel();
}

ParseFile::~ParseFile() {
  if (!m_keepParserHandler) delete m_antlrParserHandler;
  delete m_listener;
}

SymbolTable* ParseFile::getSymbolTable() {
  return m_symbolTable ? m_symbolTable : m_compileSourceFile->getSymbolTable();
}

ErrorContainer* ParseFile::getErrorContainer() {
  return m_errors ? m_errors : m_compileSourceFile->getErrorContainer();
}

SymbolId ParseFile::registerSymbol(std::string_view symbol) {
  return getCompileSourceFile()->getSymbolTable()->registerSymbol(symbol);
}

SymbolId ParseFile::getId(std::string_view symbol) const {
  return getCompileSourceFile()->getSymbolTable()->getId(symbol);
}

std::string_view ParseFile::getSymbol(SymbolId id) const {
  return getCompileSourceFile()->getSymbolTable()->getSymbol(id);
}

void ParseFile::addError(Error& error) {
  getCompileSourceFile()->getErrorContainer()->addError(error);
}

PathId ParseFile::getFileId(uint32_t line) {
  if (!getCompileSourceFile()) return m_fileId;

  PreprocessFile* pp = getCompileSourceFile()->getPreprocessor();
  if (!pp) return BadPathId;

  const auto& infos = pp->getIncludeFileInfo();
  if (infos.empty()) return m_fileId;

  if (m_locationCache.empty()) buildLocationCache();

  if ((line >= 0) && (line < m_locationCache.size())) {
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

  if ((line >= 0) && (line < m_locationCache.size())) {
    return std::get<2>(m_locationCache[line][0]);
  }

  SymbolId fileId = registerSymbol("LINE CACHE OUT OF BOUND");
  Location ppfile(fileId);
  Error err(ErrorDefinition::PA_INTERNAL_WARNING, ppfile);
  addError(err);

  return line;
}

void ParseFile::printLocationCache() {
  uint32_t index = 0;
  for (const auto& entry1 : m_locationCache) {
    for (const auto& entry2 : entry1) {
      std::cout << index << ": " << std::get<0>(entry2) << ", "
                << PathIdPP(std::get<1>(entry2)) << ", " << std::get<2>(entry2)
                << ", " << std::get<3>(entry2) << ", " << std::get<4>(entry2)
                << std::endl;
      // std::cout << index << ": " << PathIdPP(std::get<1>(entry2)) << ", "
      //           << std::get<2>(entry2) << ", " << std::get<0>(entry2) << ", "
      //           << std::get<3>(entry2) << std::endl;
    }
    std::cout << std::endl;
    ++index;
  }
}

inline bool ParseFile::isEmbeddedMacro(int32_t index) const {
  auto const& infos =
      getCompileSourceFile()->getPreprocessor()->getIncludeFileInfo();
  if ((index < 0) || (index >= infos.size())) return false;

  if (infos[index].m_context != IncludeFileInfo::Context::MACRO) {
    return false;
  }

  int32_t pops = 0;
  while (--index >= 0) {
    if (infos[index].m_context == IncludeFileInfo::Context::MACRO) {
      if (infos[index].m_action == IncludeFileInfo::Action::PUSH) {
        if (pops-- == 0) return true;
      } else if (infos[index].m_action == IncludeFileInfo::Action::POP) {
        ++pops;
      }
    }
  }

  return false;
}

inline bool ParseFile::isEmbeddedMacro(int32_t inner, int32_t outer) const {
  auto const& infos =
      getCompileSourceFile()->getPreprocessor()->getIncludeFileInfo();
  if ((outer < 0) || (outer >= infos.size())) return false;
  if ((inner < 0) || (inner >= infos.size())) return false;

  if ((infos[outer].m_context != IncludeFileInfo::Context::MACRO) ||
      (infos[inner].m_context != IncludeFileInfo::Context::MACRO)) {
    return false;
  }

  int32_t pops = 0;
  while (--inner >= outer) {
    if (infos[inner].m_context == IncludeFileInfo::Context::MACRO) {
      if (infos[inner].m_action == IncludeFileInfo::Action::PUSH) {
        if (pops-- == 0) return (inner == outer);
      } else if (infos[inner].m_action == IncludeFileInfo::Action::POP) {
        ++pops;
      }
    }
  }

  return false;
}

inline void ParseFile::addLocationCacheEntry(uint32_t sourceLine,
                                             uint32_t sourceColumn,
                                             PathId fileId, uint32_t targetLine,
                                             int32_t offset, int32_t hint) {
  m_locationCache[sourceLine].emplace_back(sourceColumn, fileId, targetLine,
                                           offset, hint);
}

void ParseFile::buildLocationCache_recurse_for_includes(uint32_t index) {
  auto const& infos =
      getCompileSourceFile()->getPreprocessor()->getIncludeFileInfo();
  const IncludeFileInfo& oifi = infos[index];
  const IncludeFileInfo& cifi = infos[oifi.m_indexOpposite];

  uint32_t sourceLine = oifi.m_sourceLine;
  uint32_t targetLine = 1;

  // NOTES:
  // sectionLine and sectionColumn are always 1, and so need no compensation
  // Every included always starts with line 1 i.e. targetLine = 1

  // In case, the include tag doesn't start at column 1
  if (oifi.m_sourceColumn > 1) {
    addLocationCacheEntry(sourceLine++, oifi.m_sourceColumn,
                          oifi.m_sectionFileId, targetLine++, 0, index);
  }

  int32_t pic = -1;
  for (int32_t i = index + 1; i < oifi.m_indexOpposite; ++i) {
    const IncludeFileInfo& ioifi = infos[i];
    if (ioifi.m_action != IncludeFileInfo::Action::PUSH) {
      continue;
    }

    const IncludeFileInfo& icifi = infos[ioifi.m_indexOpposite];

    if (ioifi.m_context == IncludeFileInfo::Context::INCLUDE) {
      if ((pic >= 0) &&
          (infos[pic].m_context != IncludeFileInfo::Context::INCLUDE)) {
        pic = -1;
      }

      if (pic >= 0) {
        // Multiple includes on the same line

        const IncludeFileInfo& picifi = infos[pic];
        const IncludeFileInfo& pioifi = infos[picifi.m_indexOpposite];

        if (picifi.m_sourceColumn > 1) {
          // Included file doesn't have a new line at the end of the file.
          // To compensate for anything (like a comment) after the input tag
          // `#include "abc.h" // This is a comment
          addLocationCacheEntry(sourceLine, picifi.m_sourceColumn,
                                oifi.m_sectionFileId, targetLine - 1,
                                int32_t(picifi.m_sourceColumn) -
                                    int32_t(pioifi.m_symbolEndColumn),
                                index);
        }

        if (picifi.m_sourceLine != ioifi.m_sourceLine) {
          ++sourceLine;

          while (sourceLine <= ioifi.m_sourceLine) {
            addLocationCacheEntry(sourceLine++, 1, oifi.m_sectionFileId,
                                  targetLine++, 0, index);
          }
        }
      } else {
        while (sourceLine <= ioifi.m_sourceLine) {
          addLocationCacheEntry(sourceLine++, 1, oifi.m_sectionFileId,
                                targetLine++, 0, index);
        }
      }

      buildLocationCache_recurse_for_includes(i);

      sourceLine = icifi.m_sourceLine;
      targetLine += (ioifi.m_symbolEndLine - ioifi.m_symbolStartLine);
      pic = ioifi.m_indexOpposite;
    } else if (ioifi.m_context == IncludeFileInfo::Context::MACRO) {
      if (pic >= 0) {
        const IncludeFileInfo& picifi = infos[pic];
        if (picifi.m_sourceLine != ioifi.m_sourceLine) {
          ++sourceLine;
        }
      }

      while (sourceLine < ioifi.m_sourceLine) {
        addLocationCacheEntry(sourceLine++, 1, oifi.m_sectionFileId,
                              targetLine++, 0, index);
      }

      while (sourceLine <= icifi.m_sourceLine) {
        addLocationCacheEntry(sourceLine++, 1, oifi.m_sectionFileId, targetLine,
                              0, index);
      }

      sourceLine = icifi.m_sourceLine;
      targetLine += (ioifi.m_symbolEndLine - ioifi.m_symbolStartLine) + 1;
      pic = ioifi.m_indexOpposite;
    }

    i = ioifi.m_indexOpposite;
  }

  if (pic >= 0) {
    const IncludeFileInfo& picifi = infos[pic];
    const IncludeFileInfo& pioifi = infos[picifi.m_indexOpposite];

    if ((picifi.m_context == IncludeFileInfo::Context::INCLUDE) &&
        (picifi.m_sourceColumn > 1)) {
      addLocationCacheEntry(
          sourceLine, picifi.m_sourceColumn, oifi.m_sectionFileId,
          targetLine - 1,
          int32_t(picifi.m_sourceColumn) - int32_t(pioifi.m_symbolEndColumn),
          index);
    }

    if (picifi.m_sourceLine < cifi.m_sourceLine) {
      ++sourceLine;
    }
  }

  while (sourceLine <= cifi.m_sourceLine) {
    addLocationCacheEntry(sourceLine++, 1, oifi.m_sectionFileId, targetLine++,
                          0, index);
  }
}

void ParseFile::buildLocationCache_recurse_for_macros(
    uint32_t index, int32_t parentIndex,
    const macro_token_offsets_t& parentOffsets) {
  auto const& infos =
      getCompileSourceFile()->getPreprocessor()->getIncludeFileInfo();
  const IncludeFileInfo& oifi = infos[index];
  const IncludeFileInfo& cifi = infos[oifi.m_indexOpposite];

  uint32_t sourceLine = oifi.m_sourceLine;
  uint32_t targetLine = 1;

  PathId macroFileId = oifi.m_sectionFileId;
  uint32_t macroStartLine = oifi.m_sourceLine;
  uint32_t macroStartColumn = oifi.m_sourceColumn;
  uint32_t macroEndLine = cifi.m_sourceLine;
  uint32_t macroEndColumn = cifi.m_sourceColumn;

  if (oifi.m_macroDefinition != nullptr) {
    macroFileId = oifi.m_macroDefinition->m_fileId;
    macroStartLine = oifi.m_macroDefinition->m_startLine;
    macroStartColumn = oifi.m_macroDefinition->m_bodyStartColumn;
    macroEndLine = oifi.m_macroDefinition->m_endLine;
    macroEndColumn = oifi.m_macroDefinition->m_endColumn;
  }

  // Handling four different cases for macros
  //  1. Single line instance expands to single line
  //  2. Single line instance expands to multi line
  //  3. Multi line instance expands to single line
  //  4. Multi line instance expands to multi line
  //     (not necessary the same number of lines)

  macro_token_offsets_t offsets;
  if (oifi.m_context == IncludeFileInfo::Context::MACRO) {
    targetLine = macroStartLine;

    const std::vector<LineColumn>& targetPositions =
        oifi.m_macroDefinition->m_positions;
    const std::vector<LineColumn>& sourcePositions = oifi.m_tokenPositions;

    int32_t delta = 0;
    uint32_t prevSourceLine = 0;
    for (uint32_t i = 0, ni = oifi.m_tokenPositions.size(); i < ni; ++i) {
      if (prevSourceLine != sourcePositions[i].first) {
        prevSourceLine = sourcePositions[i].first;
        delta = 0;
      }
      if ((sourcePositions[i].second - delta) != targetPositions[i].second) {
        if (sourcePositions[i].first == targetLine) {
          delta = (oifi.m_sourceColumn + sourcePositions[i].second - 1) -
                  targetPositions[i].second;
          offsets.emplace_back(
              sourcePositions[i].first,
              sourcePositions[i].second + oifi.m_sourceColumn - 1, delta);
          delta -= oifi.m_sourceColumn - 1;
        } else {
          delta = sourcePositions[i].second - targetPositions[i].second;
          offsets.emplace_back(sourcePositions[i].first,
                               sourcePositions[i].second, delta);
        }
      }
    }

    if (oifi.m_sourceLine == cifi.m_sourceLine) {
      if (oifi.m_symbolStartLine == oifi.m_symbolEndLine) {
        // Scenario 1. Single line instance expands to single line
        if (!isEmbeddedMacro(index)) {
          addLocationCacheEntry(
              sourceLine, oifi.m_sourceColumn, macroFileId, targetLine,
              (cifi.m_sourceColumn - oifi.m_sourceColumn) -
                  (oifi.m_symbolEndColumn - oifi.m_symbolStartColumn),
              index);
        }
      } else {
        // Scenario 3. Multi line instance expands to single line
        // addLocationCacheEntry(sourceLine,
        //     macroFileId, targetLine,
        //     cifi.m_sourceColumn, -cifi.m_symbolEndColumn);
      }
    } else {
      if (oifi.m_symbolStartLine == oifi.m_symbolEndLine) {
        // Scenario 2. Single line instance expands to multi line
        // if (!isEmbeddedMacro(index)) {
        //   addLocationCacheEntry(
        //       sourceLine, macroFileId, targetLine, oifi.m_sourceColumn,
        //       -(oifi.m_symbolEndColumn - oifi.m_symbolStartColumn), index);
        // }
      } else {
        // Scenario 4. Multi line instance expands to multi line
        addLocationCacheEntry(sourceLine, oifi.m_sourceColumn, macroFileId,
                              targetLine, -macroStartColumn, index);
      }
    }

    for (const auto& [line, column, offset] : offsets) {
      if (line == targetLine) {
        addLocationCacheEntry(sourceLine, column, macroFileId, targetLine,
                              offset, index);
      } else if (line > targetLine) {
        break;
      }
    }

    ++sourceLine;
    ++targetLine;
  }

  uint32_t prevEndSourceLine = 0;
  uint16_t prevEndSourceColumn = 0;
  for (int32_t i = index + 1; i < oifi.m_indexOpposite; ++i) {
    const IncludeFileInfo& ioifi = infos[i];
    if ((ioifi.m_action == IncludeFileInfo::Action::PUSH) &&
        (ioifi.m_context == IncludeFileInfo::Context::MACRO)) {
      const IncludeFileInfo& icifi = infos[ioifi.m_indexOpposite];
      if (prevEndSourceLine > 0) {
        if (prevEndSourceLine < ioifi.m_sourceLine) ++sourceLine;
        if (prevEndSourceLine == ioifi.m_sourceLine) --targetLine;
      }

      while (sourceLine <= ioifi.m_sourceLine) {
        if (prevEndSourceLine != ioifi.m_sourceLine) {
          addLocationCacheEntry(sourceLine, 1, macroFileId, targetLine, 0,
                                index);
        }

        for (const auto& [line, column, offset] : offsets) {
          if (line == targetLine) {
            if ((prevEndSourceColumn < column) &&
                (column < ioifi.m_sourceColumn)) {
              addLocationCacheEntry(sourceLine, column, macroFileId, targetLine,
                                    offset, index);
            } else {
              break;
            }
          } else if (line > targetLine) {
            break;
          }
        }

        ++sourceLine;
        ++targetLine;
      }

      buildLocationCache_recurse_for_macros(i, index, offsets);
      targetLine += (ioifi.m_symbolEndLine - ioifi.m_symbolStartLine);
      prevEndSourceLine = sourceLine = icifi.m_sourceLine;
      prevEndSourceColumn = icifi.m_sourceColumn;
    }

    i = ioifi.m_indexOpposite;
  }

  if (oifi.m_context == IncludeFileInfo::Context::MACRO) {
    if (prevEndSourceLine > 0) ++sourceLine;
    while (sourceLine <= cifi.m_sourceLine) {
      addLocationCacheEntry(sourceLine, 1, macroFileId, targetLine, 0, index);

      for (const auto& [line, column, offset] : offsets) {
        if (line == targetLine) {
          addLocationCacheEntry(sourceLine, column, macroFileId, targetLine,
                                offset, index);
        } else if (line > targetLine) {
          break;
        }
      }

      ++sourceLine;
      ++targetLine;
    }

    uint16_t symbolEndColumn = oifi.m_symbolEndColumn;
    for (const auto& [line, column, offset] : parentOffsets) {
      if ((line == oifi.m_symbolEndLine) &&
          (oifi.m_symbolEndColumn >= column)) {
        symbolEndColumn = oifi.m_symbolEndColumn - offset;
      } else if (line > oifi.m_symbolEndLine) {
        break;
      }
    }

    if (oifi.m_sourceLine == cifi.m_sourceLine) {
      // Scenario 1. Single line instance expands to single line
      if (oifi.m_symbolStartLine == oifi.m_symbolEndLine) {
        addLocationCacheEntry(oifi.m_sourceLine, cifi.m_sourceColumn,
                              oifi.m_sectionFileId, oifi.m_symbolEndLine,
                              cifi.m_sourceColumn - symbolEndColumn,
                              parentIndex);
      } else {
        // Scenario 3. Multi line instance expands to single line
        addLocationCacheEntry(oifi.m_sourceLine, cifi.m_sourceColumn,
                              oifi.m_sectionFileId, oifi.m_symbolEndLine,
                              cifi.m_sourceColumn - symbolEndColumn,
                              parentIndex);
      }
    } else {
      if (oifi.m_symbolStartLine == oifi.m_symbolEndLine) {
        // Scenario 2. Single line instance expands to multi line
        addLocationCacheEntry(cifi.m_sourceLine, cifi.m_sourceColumn,
                              oifi.m_sectionFileId, oifi.m_symbolEndLine,
                              cifi.m_sourceColumn - symbolEndColumn,
                              parentIndex);
      } else {
        // Scenario 4. Multi line instance expands to multi line
        addLocationCacheEntry(cifi.m_sourceLine, cifi.m_sourceColumn,
                              oifi.m_sectionFileId, oifi.m_symbolEndLine,
                              cifi.m_sourceColumn - symbolEndColumn,
                              parentIndex);
      }
    }
  }
}

void ParseFile::buildLocationCache() {
  PreprocessFile* pp = getCompileSourceFile()->getPreprocessor();
  if (!pp) return;

  auto const& infos = pp->getIncludeFileInfo();
  if (infos.empty()) return;

  m_locationCache.clear();
  m_locationCache.resize(pp->getLineCount() + 10);
  m_locationCache[0].emplace_back(0, m_fileId, 0, 0, -1);
  buildLocationCache_recurse_for_includes(0);
  buildLocationCache_recurse_for_macros(0, -1, {});

  uint32_t sourceLine = infos.back().m_sourceLine + 1;
  uint32_t targetLine = std::get<2>(m_locationCache[sourceLine - 1].back()) + 1;
  while (sourceLine < m_locationCache.size()) {
    m_locationCache[sourceLine++].emplace_back(1, m_fileId, targetLine++, 0, 0);
  }
  // printLocationCache();
}

std::tuple<PathId, uint32_t, uint16_t> ParseFile::mapLocation(uint32_t line,
                                                              uint16_t column,
                                                              bool isStart) {
  if ((line == 0) || (column == 0)) return {m_fileId, line, column};
  if (!getCompileSourceFile()) return {m_fileId, line, column};

  PreprocessFile* pp = getCompileSourceFile()->getPreprocessor();
  if (!pp) return {m_fileId, line, column};

  auto& infos = pp->getIncludeFileInfo();
  if (infos.empty()) return {BadPathId, 0, 0};

  if (m_locationCache.empty()) buildLocationCache();

  if ((line >= 1) && (line < m_locationCache.size())) {
    const location_cache_entry_t& entry = m_locationCache[line];
    const uint16_t columnInSource = column;

    PathId fileId = m_fileId;
    int32_t index = -1;
    for (uint32_t i = 0, ni = entry.size(); i < ni; ++i) {
      const location_cache_entry_t::value_type& item = entry[i];

      if (columnInSource >= std::get<0>(item)) {
        index = i;
      }
    }

    if (!isStart && (index > 0)) {
      // const location_cache_entry_t::value_type& item_p = entry[index - 1];
      const location_cache_entry_t::value_type& item_i = entry[index];

      if (columnInSource == std::get<0>(item_i)) --index;
    }

    if (index >= 0) {
      const location_cache_entry_t::value_type& item = entry[index];
      fileId = std::get<1>(item);
      line = std::get<2>(item);
      column = columnInSource - std::get<3>(item);
    }

    return {fileId, line, column};
  }

  SymbolId fileId = registerSymbol("Out of bound request to map location");
  Location ppfile(fileId);
  Error err(ErrorDefinition::PA_INTERNAL_WARNING, ppfile);
  addError(err);

  return {m_fileId, line, column};
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

  // if ((si > 0) && (ei >= 0)) {
  //   const location_cache_entry_t::value_type& item_s = entry_s[si];
  //   const location_cache_entry_t::value_type& item_s_p = entry_s[si - 1];
  // 
  //   const location_cache_entry_t::value_type& item_e = entry_e[ei];
  // 
  //   const int32_t hint_s = std::get<4>(item_s);
  //   const int32_t hint_e = std::get<4>(item_e);
  //   const int32_t hint_s_p = std::get<4>(item_s_p);
  // 
  //   const IncludeFileInfo& info_s = infos[hint_s];
  // 
  //   if ((hint_s >= 0) &&
  //       (info_s.m_context == IncludeFileInfo::Context::MACRO) &&
  //       ((hint_e < 0) || (hint_s != hint_e)) && (hint_s_p == hint_e) &&
  //       (sc == std::get<0>(item_s))) {
  //     --si;
  //   }
  // }
  // 
  // if ((si >= 0) && (ei > 0)) {
  //   const location_cache_entry_t::value_type& item_s = entry_s[si];
  // 
  //   const location_cache_entry_t::value_type& item_e = entry_e[ei];
  //   const location_cache_entry_t::value_type& item_e_p = entry_e[ei - 1];
  // 
  //   const int32_t hint_s = std::get<4>(item_s);
  //   const int32_t hint_e = std::get<4>(item_e);
  //   const int32_t hint_e_p = std::get<4>(item_e_p);
  // 
  //   const IncludeFileInfo& info_s = infos[hint_s];
  // 
  //   if ((hint_s >= 0) &&
  //       (info_s.m_context == IncludeFileInfo::Context::MACRO) &&
  //       ((hint_e < 0) || (hint_s != hint_e)) && (hint_s == hint_e_p) &&
  //       (ec == std::get<0>(item_e))) {
  //     --ei;
  //   }
  // }

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

  // if ((si >= 0) && (ei >= 0)) {
  //   const location_cache_entry_t::value_type& item_s = entry_s[si];
  //   const location_cache_entry_t::value_type& item_e = entry_e[ei];
  // 
  //   const int32_t hint_s = std::get<4>(item_s);
  //   const int32_t hint_e = std::get<4>(item_e);
  // 
  //   if (hint_s != hint_e) {
  //     const IncludeFileInfo& info_s = infos[hint_s];
  //     const IncludeFileInfo& info_e = infos[hint_e];
  // 
  //     if (isEmbeddedMacro(hint_s, hint_e)) {
  //       csl = cel;
  //       csc = info_s.m_symbolStartColumn;
  //     } else if (isEmbeddedMacro(hint_e, hint_s)) {
  //       cel = csl;
  //       cec = info_e.m_symbolEndColumn;
  //     }
  //   }
  // }

  return {csf, csl, csc, cef, cel, cec};
}

bool ParseFile::parseOneFile_(PathId fileId, uint32_t lineOffset) {
  FileSystem* const fileSystem = FileSystem::getInstance();
  CommandLineParser* clp = getCompileSourceFile()->getCommandLineParser();
  PreprocessFile* pp = getCompileSourceFile()->getPreprocessor();
  Timer tmr;
  m_antlrParserHandler = new AntlrParserHandler();
  m_antlrParserHandler->m_clearAntlrCache = clp->lowMem();
  if (m_sourceText.empty()) {
    std::istream& stream = fileSystem->openForRead(fileId);
    if (!stream.good()) {
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
      new AntlrParserErrorListener(this, false, lineOffset, fileId);
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
    std::string_view type = std::get<1>(
        fileSystem->getType(fileId, getCompileSourceFile()->getSymbolTable()));
    m_antlrParserHandler->m_lexer->sverilog =
        (type == ".sv") || clp->fullSVMode() || clp->isSVFile(fileId);
  }

  m_antlrParserHandler->m_lexer->removeErrorListeners();
  m_antlrParserHandler->m_lexer->addErrorListener(
      m_antlrParserHandler->m_errorListener);
  m_antlrParserHandler->m_tokens =
      new antlr4::CommonTokenStream(m_antlrParserHandler->m_lexer);
  m_antlrParserHandler->m_tokens->fill();

  if (getCompileSourceFile()->getCommandLineParser()->profile()) {
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

  if (getCompileSourceFile()->getCommandLineParser()->profile()) {
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

    if (getCompileSourceFile()->getCommandLineParser()->profile()) {
      StrAppend(&m_profileInfo,
                "SLL Parsing: ", StringUtils::to_string(tmr.elapsed_rounded()),
                "s ", fileSystem->toPath(fileId), "\n");
      tmr.reset();
      profileParser();
    }
  } catch (antlr4::ParseCancellationException& pex) {
    m_antlrParserHandler->m_tokens->reset();
    m_antlrParserHandler->m_parser->reset();
    m_antlrParserHandler->m_parser->removeErrorListeners();
    if (getCompileSourceFile()->getCommandLineParser()->profile()) {
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

    if (getCompileSourceFile()->getCommandLineParser()->profile()) {
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
  FileSystem* const fileSystem = FileSystem::getInstance();
  CommandLineParser* clp = getCompileSourceFile()->getCommandLineParser();
  Precompiled* prec = Precompiled::getSingleton();
  bool precompiled = prec->isFilePrecompiled(m_ppFileId, getSymbolTable());

  if (m_children.empty()) {
    ParseCache cache(this);

    if (cache.restore()) {
      m_usingCachedVersion = true;
      if (debug_AstModel && !precompiled)
        std::cout << m_fileContent->printObjects();
      if (clp->debugCache()) {
        std::cout << "PARSER CACHE USED FOR: "
                  << fileSystem->toPath(getFileId(0)) << std::endl;
      }
      return true;
    }
  } else {
    bool ok = true;
    for (ParseFile* child : m_children) {
      ParseCache cache(child);

      if (cache.restore()) {
        child->m_fileContent->setParent(m_fileContent);
        m_usingCachedVersion = true;
        if (debug_AstModel && !precompiled)
          std::cout << child->m_fileContent->printObjects();
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

      m_listener = new SV3_1aTreeShapeListener(
          this, m_antlrParserHandler->m_tokens, m_offsetLine);
      antlr4::tree::ParseTreeWalker::DEFAULT.walk(m_listener,
                                                  m_antlrParserHandler->m_tree);

      if (debug_AstModel && !precompiled)
        std::cout << m_fileContent->printObjects();

      if (clp->profile()) {
        // m_profileInfo += "AST Walking: " + std::to_string
        // (tmr.elapsed_rounded ()) + "\n";
        tmr.reset();
      }

      ParseCache cache(this);
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
          child->m_listener = new SV3_1aTreeShapeListener(
              child, child->m_antlrParserHandler->m_tokens,
              child->m_offsetLine);

          Timer tmr;
          antlr4::tree::ParseTreeWalker::DEFAULT.walk(
              child->m_listener, child->m_antlrParserHandler->m_tree);

          if (clp->profile()) {
            // m_profileInfo += "For file " + getSymbol
            // (child->m_ppFileId) + ", AST Walking took" +
            // std::to_string (tmr.elapsed_rounded ()) + "\n";
            tmr.reset();
          }

          if (debug_AstModel && !precompiled)
            std::cout << child->m_fileContent->printObjects();

          ParseCache cache(child);
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
