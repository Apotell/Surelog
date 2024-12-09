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
 * File:   ParseFile.h
 * Author: alain
 *
 * Created on February 24, 2017, 10:03 PM
 */

#ifndef SURELOG_PARSEFILE_H
#define SURELOG_PARSEFILE_H
#pragma once

#include <Surelog/ErrorReporting/Error.h>

#include <string>
#include <tuple>
#include <vector>

namespace SURELOG {
class AntlrParserErrorListener;
class AntlrParserHandler;
class CompilationUnit;
class CompileSourceFile;
class FileContent;
class Library;
class Session;
class SV3_1aParserBaseListener;
class SV3_1aPythonListener;
class SV3_1aTreeShapeListener;

class ParseFile final {
 public:
  friend class PythonListen;

  // Helper constructor used by SVLibShapeListener
  ParseFile(Session* session, PathId fileId);

  // Regular file
  ParseFile(Session* session, PathId fileId, CompileSourceFile* csf,
            CompilationUnit* compilationUnit, Library* library, PathId ppFileId,
            bool keepParserHandler);

  // File chunk
  ParseFile(Session* session, CompileSourceFile* compileSourceFile,
            ParseFile* parent, PathId chunkFileId, uint32_t offsetLine);

  // Unit test constructor
  ParseFile(Session* session, std::string_view text, CompileSourceFile* csf,
            CompilationUnit* compilationUnit, Library* library);
  ~ParseFile();

  bool parse();
  bool needToParse();

  CompileSourceFile* getCompileSourceFile() const {
    return m_compileSourceFile;
  }
  CompilationUnit* getCompilationUnit() const { return m_compilationUnit; }
  Library* getLibrary() const { return m_library; }
  PathId getFileId(uint32_t line);
  PathId getRawFileId() const { return m_fileId; }
  PathId getPpFileId() const { return m_ppFileId; }
  uint32_t getLineNb(uint32_t line);

  std::tuple<PathId, uint32_t, uint16_t, PathId, uint32_t, uint16_t>
  mapLocations(uint32_t sl, uint16_t sc, uint32_t el, uint16_t ec);

  class LineTranslationInfo {
   public:
    LineTranslationInfo(PathId pretendFileId, uint32_t originalLine,
                        uint32_t pretendLine)
        : m_pretendFileId(pretendFileId),
          m_originalLine(originalLine),
          m_pretendLine(pretendLine) {}
    PathId m_pretendFileId;
    uint32_t m_originalLine;
    uint32_t m_pretendLine;
  };

  AntlrParserHandler* getAntlrParserHandler() const {
    return m_antlrParserHandler;
  }

  void addLineTranslationInfo(LineTranslationInfo& info) {
    m_lineTranslationVec.push_back(info);
  }

  void addError(Error& error);
  SymbolId registerSymbol(std::string_view symbol);
  SymbolId getId(std::string_view symbol) const;
  std::string_view getSymbol(SymbolId id) const;
  bool usingCachedVersion() const { return m_usingCachedVersion; }
  FileContent* getFileContent() { return m_fileContent; }
  void setFileContent(FileContent* content) { m_fileContent = content; }
  void setDebugAstModel() { debug_AstModel = true; }
  std::string getProfileInfo() const;
  void profileParser();

 private:
  using location_cache_entry_t =
      std::vector<std::tuple<uint32_t, PathId, uint32_t, int32_t, int32_t>>;
  using location_cache_t = std::vector<location_cache_entry_t>;
  using token_offsets_t =
      std::vector<std::vector<std::tuple<uint32_t, uint16_t, int32_t>>>;

  bool parseOneFile_(PathId fileId, uint32_t lineOffset);
  void buildLocationCache();
  void buildLocationCache_recurse(uint32_t index,
                                  const token_offsets_t& offsets);
  void printLocationCache() const;

 private:
  Session* const m_session = nullptr;
  PathId m_fileId;
  PathId m_ppFileId;
  CompileSourceFile* const m_compileSourceFile = nullptr;
  CompilationUnit* const m_compilationUnit = nullptr;
  Library* m_library = nullptr;
  AntlrParserHandler* m_antlrParserHandler = nullptr;
  SV3_1aParserBaseListener* m_listener = nullptr;
  std::vector<LineTranslationInfo> m_lineTranslationVec;
  bool m_usingCachedVersion = false;
  bool m_keepParserHandler = false;
  FileContent* m_fileContent = nullptr;
  bool debug_AstModel = false;

  // For file chunk:
  std::vector<ParseFile*> m_children;
  ParseFile* const m_parent = nullptr;
  uint32_t m_offsetLine = 0;
  std::string m_profileInfo;
  std::string m_sourceText;  // For Unit tests
  location_cache_t m_locationCache;
};

};  // namespace SURELOG

#endif /* SURELOG_PARSEFILE_H */
