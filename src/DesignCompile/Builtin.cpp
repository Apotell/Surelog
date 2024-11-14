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
 * File:   Builtin.cpp
 * Author: alain
 *
 * Created on May 30, 2019, 6:36 PM
 */

#include "Surelog/DesignCompile/Builtin.h"

#include "Surelog/Common/FileSystem.h"
#include "Surelog/Common/NodeId.h"
#include "Surelog/Common/PathId.h"
#include "Surelog/Design/Design.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/DesignCompile/CompileDesign.h"
#include "Surelog/DesignCompile/CompileHelper.h"
#include "Surelog/DesignCompile/CompilerHarness.h"
#include "Surelog/Library/Library.h"
#include "Surelog/Package/Package.h"
#include "Surelog/SourceCompile/Compiler.h"
#include "Surelog/SourceCompile/ParserHarness.h"
#include "Surelog/SourceCompile/PreprocessHarness.h"
#include "Surelog/SourceCompile/SymbolTable.h"
#include "Surelog/SourceCompile/VObjectTypes.h"
#include "Surelog/Testbench/ClassDefinition.h"
#include "Surelog/Testbench/FunctionMethod.h"
#include "Surelog/Utils/StringUtils.h"

// UHDM
#include <uhdm/Serializer.h>
#include <uhdm/package.h>

#include <string>
#include <string_view>
#include <vector>

namespace SURELOG {

static VObjectType convert(std::string_view type) {
  VObjectType result = VObjectType::slNoType;
  if (type == "int")
    result = VObjectType::paIntegerAtomType_Int;
  else if (type == "generic")
    result = VObjectType::paGenericElementType;
  return result;
}

void Builtin::addBuiltinTypes() {
  struct FunctionDefinition {
    constexpr FunctionDefinition(std::string_view packageName,
                                 std::string_view className,
                                 std::string_view returnTypeName,
                                 std::string_view functionName,
                                 std::string_view unused1 = "",
                                 std::string_view unused2 = "")
        : packageName(packageName),
          className(className),
          returnTypeName(returnTypeName),
          functionName(functionName) {}

    std::string_view packageName;
    std::string_view className;
    std::string_view returnTypeName;
    std::string_view functionName;
  };

  static constexpr FunctionDefinition functionDef[] = {
      {"builtin", "array", "generic", "find"},
      {"builtin", "array", "int", "find_index"},
      {"builtin", "array", "int", "find_first"},
      {"builtin", "array", "int", "find_first_index"},
      {"builtin", "array", "int", "find_last"},
      {"builtin", "array", "int", "find_last_index"},
      {"builtin", "array", "int", "min"},
      {"builtin", "array", "int", "max"},
      {"builtin", "array", "int", "unique"},
      {"builtin", "array", "int", "unique_index"},
      {"builtin", "array", "void", "reverse"},
      {"builtin", "array", "void", "sort"},
      {"builtin", "array", "void", "rsort"},
      {"builtin", "array", "void", "shuffle"},
      {"builtin", "array", "void", "sum"},
      {"builtin", "array", "void", "product"},
      {"builtin", "array", "void", "and"},
      {"builtin", "array", "void", "or"},
      {"builtin", "array", "void", "xor"},
      {"builtin", "array", "void", "delete"},

      {"builtin", "queue", "int", "size"},
      {"builtin", "queue", "void", "insert", "int", "generic"},
      {"builtin", "queue", "void", "delete", "int"},
      {"builtin", "queue", "generic", "pop_front"},
      {"builtin", "queue", "generic", "pop_back"},
      {"builtin", "queue", "void", "push_front", "generic"},
      {"builtin", "queue", "void", "push_back", "generic"},

      {"builtin", "string", "int", "len"},
      {"builtin", "string", "void", "putc", "int", "int"},
      {"builtin", "string", "int", "getc", "int"},
      {"builtin", "string", "string", "toupper"},
      {"builtin", "string", "string", "tolower"},
      {"builtin", "string", "int", "compare", "string"},
      {"builtin", "string", "int", "icompare", "string"},
      {"builtin", "string", "string", "substr", "int", "int"},
      {"builtin", "string", "int", "atoi"},
      {"builtin", "string", "real", "atoreal"},
      {"builtin", "string", "void", "itoa", "int"},
      {"builtin", "string", "void", "hextoa", "int"},
      {"builtin", "string", "void", "bintoa", "int"},
      {"builtin", "string", "void", "realtoa", "real"},

      {"builtin", "system", "void", "display", "generic"},
      {"builtin", "system", "void", "write", "generic"},
      {"builtin", "system", "void", "strobe", "generic"},
      {"builtin", "system", "void", "monitor", "generic"},
      {"builtin", "system", "void", "monitoron", "generic"},
      {"builtin", "system", "void", "monitoroff", "generic"},
      {"builtin", "system", "void", "displayb", "generic"},
      {"builtin", "system", "void", "writeb", "generic"},
      {"builtin", "system", "void", "strobeb", "generic"},
      {"builtin", "system", "void", "monitorb", "generic"},
      {"builtin", "system", "void", "displayo", "generic"},
      {"builtin", "system", "void", "writeo", "generic"},
      {"builtin", "system", "void", "strobeo", "generic"},
      {"builtin", "system", "void", "monitoro", "generic"},
      {"builtin", "system", "void", "displayh", "generic"},
      {"builtin", "system", "void", "writeh", "generic"},
      {"builtin", "system", "void", "strobeh", "generic"},
      {"builtin", "system", "void", "monitorh", "generic"},
      {"builtin", "system", "void", "fopen", "generic"},
      {"builtin", "system", "void", "fclose", "generic"},
      {"builtin", "system", "void", "frewind", "generic"},
      {"builtin", "system", "void", "fflush", "generic"},
      {"builtin", "system", "void", "fseek", "generic"},
      {"builtin", "system", "void", "ftell", "generic"},
      {"builtin", "system", "void", "fdisplay", "generic"},
      {"builtin", "system", "void", "fwrite", "generic"},
      {"builtin", "system", "void", "swrite", "generic"},
      {"builtin", "system", "void", "fstrobe", "generic"},
      {"builtin", "system", "void", "fmonitor", "generic"},
      {"builtin", "system", "void", "fread", "generic"},
      {"builtin", "system", "void", "fscanf", "generic"},
      {"builtin", "system", "void", "fdisplayb", "generic"},
      {"builtin", "system", "void", "fwriteb", "generic"},
      {"builtin", "system", "void", "swriteb", "generic"},
      {"builtin", "system", "void", "fstrobeb", "generic"},
      {"builtin", "system", "void", "fmonitorb", "generic"},
      {"builtin", "system", "void", "fdisplayo", "generic"},
      {"builtin", "system", "void", "fwriteo", "generic"},
      {"builtin", "system", "void", "swriteo", "generic"},
      {"builtin", "system", "void", "fstrobeo", "generic"},
      {"builtin", "system", "void", "fmonitoro", "generic"},
      {"builtin", "system", "void", "fdisplayh", "generic"},
      {"builtin", "system", "void", "fwriteh", "generic"},
      {"builtin", "system", "void", "swriteh", "generic"},
      {"builtin", "system", "void", "fstrobeh", "generic"},
      {"builtin", "system", "void", "fmonitorh", "generic"},
      {"builtin", "system", "void", "sscanf", "generic"},
      {"builtin", "system", "void", "sdf_annotate", "generic"},
      {"builtin", "system", "void", "sformat", "generic"},
      {"builtin", "system", "void", "cast", "generic"},
      {"builtin", "system", "void", "assertkill", "generic"},
      {"builtin", "system", "void", "assertoff", "generic"},
      {"builtin", "system", "void", "asserton", "generic"},
      {"builtin", "system", "void", "bits", "generic"},
      {"builtin", "system", "void", "bitstoshortreal", "generic"},
      {"builtin", "system", "void", "countones", "generic"},
      {"builtin", "system", "void", "coverage_control", "generic"},
      {"builtin", "system", "void", "coverage_merge", "generic"},
      {"builtin", "system", "void", "coverage_save", "generic"},
      {"builtin", "system", "void", "dimensions", "generic"},
      {"builtin", "system", "void", "error", "generic"},
      {"builtin", "system", "void", "exit", "generic"},
      {"builtin", "system", "void", "fatal", "generic"},
      {"builtin", "system", "void", "fell", "generic"},
      {"builtin", "system", "void", "get_coverage", "generic"},
      {"builtin", "system", "void", "coverage_get", "generic"},
      {"builtin", "system", "void", "coverage_get_max", "generic"},
      {"builtin", "system", "void", "high", "generic"},
      {"builtin", "system", "void", "increment", "generic"},
      {"builtin", "system", "void", "info", "generic"},
      {"builtin", "system", "void", "isunbounded", "generic"},
      {"builtin", "system", "void", "isunknown", "generic"},
      {"builtin", "system", "void", "left", "generic"},
      {"builtin", "system", "void", "load_coverage_db", "generic"},
      {"builtin", "system", "void", "low", "generic"},
      {"builtin", "system", "void", "onehot", "generic"},
      {"builtin", "system", "void", "past", "generic"},
      {"builtin", "system", "void", "readmemb", "generic"},
      {"builtin", "system", "void", "readmemh", "generic"},
      {"builtin", "system", "void", "right", "generic"},
      {"builtin", "system", "void", "root", "generic"},
      {"builtin", "system", "void", "rose", "generic"},
      {"builtin", "system", "void", "sampled", "generic"},
      {"builtin", "system", "void", "set_coverage_db_name", "generic"},
      {"builtin", "system", "void", "shortrealtobits", "generic"},
      {"builtin", "system", "void", "size", "generic"},
      {"builtin", "system", "void", "stable", "generic"},
      {"builtin", "system", "void", "typename", "generic"},
      {"builtin", "system", "void", "typeof", "generic"},
      {"builtin", "system", "void", "unit", "generic"},
      {"builtin", "system", "void", "urandom", "generic"},
      {"builtin", "system", "void", "srandom", "generic"},
      {"builtin", "system", "void", "urandom_range", "generic"},
      {"builtin", "system", "void", "set_randstate", "generic"},
      {"builtin", "system", "void", "get_randstate", "generic"},
      {"builtin", "system", "void", "dist_uniform", "generic"},
      {"builtin", "system", "void", "dist_normal", "generic"},
      {"builtin", "system", "void", "dist_exponential", "generic"},
      {"builtin", "system", "void", "dist_poisson", "generic"},
      {"builtin", "system", "void", "dist_chi_square", "generic"},
      {"builtin", "system", "void", "dist_t", "generic"},
      {"builtin", "system", "void", "dist_erlang", "generic"},
      {"builtin", "system", "void", "warning", "generic"},
      {"builtin", "system", "void", "writememb", "generic"},
      {"builtin", "system", "void", "writememh", "generic"},
      {"builtin", "system", "void", "value$plusargs", "generic"},
      {"builtin", "any_sverilog_class", "void", "randomize"},
      {"builtin", "any_sverilog_class", "void", "srandom"},
      {"builtin", "any_sverilog_class", "void", "constraint_mode"},
      {"builtin", "any_sverilog_class", "void", "rand_mode"}};

  UHDM::Serializer& s = m_compiler->getSerializer();
  for (const auto& f : functionDef) {
    Package* package = m_design->getPackage(f.packageName);
    if (package == nullptr) {
      package = new Package(f.packageName, nullptr, nullptr, InvalidNodeId);
      UHDM::package* pack = s.MakePackage();
      pack->VpiName(package->getName());
      package->setUhdmInstance(pack);
      m_design->addPackageDefinition(f.packageName, package);
    }
    const std::string fullClassName = StrCat(f.packageName, "::", f.className);
    ClassDefinition* classDef = m_design->getClassDefinition(fullClassName);
    if (classDef == nullptr) {
      classDef =
          new ClassDefinition(f.className, nullptr, package, nullptr,
                              InvalidNodeId, nullptr, s.MakeClass_defn());
      m_design->addClassDefinition(fullClassName, classDef);
      package->addClassDefinition(f.className, classDef);
    }

    DataType* dtype = new DataType(nullptr, InvalidNodeId, f.returnTypeName,
                                   convert(f.returnTypeName));
    FunctionMethod* method =
        new FunctionMethod(classDef, nullptr, InvalidNodeId, f.functionName,
                           dtype, false, false, false, false, false, false);
    classDef->insertFunction(method);
  }
}

void Builtin::addBuiltinMacros(CompilationUnit* compUnit) {
  PreprocessHarness ppharness;
  ppharness.preprocess(R"(
`define SV_COV_START 0
`define SV_COV_STOP 1
`define SV_COV_RESET 2
`define SV_COV_CHECK 3
`define SV_COV_MODULE 10
`define SV_COV_HIER 11
`define SV_COV_ASSERTION 20
`define SV_COV_FSM_STATE 21
`define SV_COV_STATEMENT 22
`define SV_COV_TOGGLE 23
`define SV_COV_OVERFLOW -2
`define SV_COV_ERROR -1
`define SV_COV_NOCOV 0
`define SV_COV_OK 1
`define SV_COV_PARTIAL 2
`define SURELOG 1
  )",
                       compUnit);
}

void Builtin::addBuiltinClasses() {
  // builtin.sv compilation
  FileSystem* const fileSystem = FileSystem::getInstance();
  UHDM::Serializer& s = m_compiler->getSerializer();
  // A fake path to keep the API simple!
  SymbolTable* const symbolTable = m_compiler->getCompiler()->getSymbolTable();
  PathId fileId = fileSystem->getChild(fileSystem->getWorkingDir(symbolTable),
                                       "builtin.sv", symbolTable);
  CompileHelper helper;
  ParserHarness pharness;
  CompilerHarness charness;
  FileContent* fC1 = pharness.parse(
      R"(  class mailbox;

    function new (int bound = 0);
    endfunction

    function int num();
    endfunction

    task put (message);
    endtask

    function try_put (message);
    endfunction

    task get (ref message);
    endtask

    function int try_get (ref message);
    endfunction

    task peek (ref message);
    endtask

    function int try_peek(ref message);
    endfunction

  endclass


  class process;

    typedef enum { FINISHED, RUNNING, WAITING, SUSPENDED, KILLED } state;

    static function process self();
    endfunction

    function state status();
    endfunction

    task kill();
    endtask

    task await();
    endtask

    task suspend();
    endtask

    task resume();
    endtask

  endclass


  class semaphore;

    function new(int keyCount = 0 );
    endfunction

    task put(int keyCount = 1);
    endtask

    task get(int keyCount = 1);
    endtask

    function int try_get(int keyCount = 1);
    endfunction

  endclass

        )",
      m_compiler->getCompiler(), fileId);

  std::vector<NodeId> classes =
      fC1->sl_collect_all(fC1->getRootNode(), VObjectType::paClass_declaration);
  m_compiler->getCompiler()->getDesign()->addFileContent(fC1->getFileId(), fC1);
  for (const auto& classId : classes) {
    NodeId stId = fC1->sl_collect(classId, VObjectType::slStringConst,
                                  VObjectType::paAttr_spec);
    const std::string_view libName = fC1->getLibrary()->getName();
    if (stId) {
      const std::string_view name = fC1->SymName(stId);
      fC1->insertObjectLookup(name, classId,
                              m_compiler->getCompiler()->getErrorContainer());
      std::string fullName = StrCat(libName, "@", name);
      ClassDefinition* def =
          new ClassDefinition(fullName, fC1->getLibrary(), nullptr, fC1,
                              classId, nullptr, s.MakeClass_defn());
      fC1->addClassDefinition(fullName, def);
      m_compiler->getCompiler()->getDesign()->addClassDefinition(name, def);
    }
  }
}
}  // namespace SURELOG
