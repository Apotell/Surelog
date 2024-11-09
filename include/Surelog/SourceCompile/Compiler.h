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
 * File:   Compiler.h
 * Author: alain
 *
 * Created on March 4, 2017, 5:16 PM
 */

#ifndef SURELOG_COMPILER_H
#define SURELOG_COMPILER_H
#pragma once

#include <Surelog/Common/PathId.h>
#include <Surelog/Common/SymbolId.h>
#include <Surelog/ErrorReporting/ErrorContainer.h>
#include <Surelog/SourceCompile/CompileSourceFile.h>
#include <Surelog/SourceCompile/PreprocessFile.h>
#include <uhdm/vpi_user.h>

#include <string_view>

#ifdef USETBB
#include <tbb/task.h>
#include <tbb/task_group.h>
#include <tbb/task_scheduler_init.h>
#endif

#include <map>
#include <set>
#include <string>
#include <vector>

namespace SURELOG {

class CommandLineParser;
class CompileDesign;
class ConfigSet;
class Design;
class ErrorContainer;
class FileContent;
class LibrarySet;
class PreprocessFile;
class SymbolTable;

class Compiler {
 public:
  using PPFileMap =
      std::map<PathId, std::vector<PathId>, PathIdLessThanComparer>;
  Compiler(CommandLineParser* commandLineParser, ErrorContainer* errors,
           SymbolTable* symbolTable);
  Compiler(CommandLineParser* commandLineParser, ErrorContainer* errors,
           SymbolTable* symbolTable, std::string_view text);
  Compiler(const Compiler& orig) = delete;
  virtual ~Compiler();

  bool compile();
  void purgeParsers();
  CommandLineParser* getCommandLineParser() const {
    return m_commandLineParser;
  }
  SymbolTable* getSymbolTable() const { return m_symbolTable; }
  ErrorContainer* getErrorContainer() const { return m_errors; }
  std::vector<CompileSourceFile*>& getCompileSourceFiles() {
    return m_compilers;
  }
  const std::map<SymbolId, PreprocessFile::AntlrParserHandler*,
                 SymbolIdLessThanComparer>&
  getPpAntlrHandlerMap() const {
    return m_antlrPpMap;
  }
  void registerAntlrPpHandlerForId(SymbolId id,
                                   PreprocessFile::AntlrParserHandler* pp);
  PreprocessFile::AntlrParserHandler* getAntlrPpHandlerForId(SymbolId);

  // TODO: this should return a const Design, but can't be because
  // of Design having a bunch of non-const accessors. Address
  // these first.
  // All _modifying_ operations should be calls on the Compiler,
  // not on the handed out Design object, as the Compiler is owner
  // of the design.
  Design* getDesign() const { return m_design; }

  vpiHandle getUhdmDesign() const { return m_uhdmDesign; }
  CompileDesign* getCompileDesign() const { return m_compileDesign; }
  ErrorContainer::Stats getErrorStats() const;
  bool isLibraryFile(PathId id) const;
  const PPFileMap& getPPFileMap() { return m_ppFileMap; }
#ifdef USETBB
  tbb::task_group& getTaskGroup() { return m_taskGroup; }
#endif

 private:
  bool parseLibrariesDef_();

  bool ppinit_();
  bool createFileList_();
  bool createMultiProcessPreProcessor_();
  bool createMultiProcessParser_();
  bool parseinit_();
  bool pythoninit_();
  bool compileFileSet_(CompileSourceFile::Action action, bool allowMultithread,
                       std::vector<CompileSourceFile*>& container);
  bool compileOneFile_(CompileSourceFile* compileSource,
                       CompileSourceFile::Action action);
  bool cleanup_();

  CommandLineParser* const m_commandLineParser;
  ErrorContainer* const m_errors;
  SymbolTable* const m_symbolTable;
  CompilationUnit* m_commonCompilationUnit;
  std::map<SymbolId, PreprocessFile::AntlrParserHandler*,
           SymbolIdLessThanComparer>
      m_antlrPpMap;
  std::vector<CompileSourceFile*> m_compilers;
  std::vector<CompileSourceFile*> m_compilersChunkFiles;
  std::vector<CompileSourceFile*> m_compilersParentFiles;
  std::vector<CompilationUnit*> m_compilationUnits;
  std::vector<SymbolTable*> m_symbolTables;
  std::vector<ErrorContainer*> m_errorContainers;
  LibrarySet* const m_librarySet;
  ConfigSet* const m_configSet;
  Design* const m_design;
  vpiHandle m_uhdmDesign;
  PathIdSet m_libraryFiles;  // -v <file>
  std::string m_text;        // unit tests
  CompileDesign* m_compileDesign;
  PPFileMap m_ppFileMap;
#ifdef USETBB
  tbb::task_group m_taskGroup;
#endif
};

};  // namespace SURELOG

#endif /* SURELOG_COMPILER_H */
