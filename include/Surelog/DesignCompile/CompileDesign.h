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
 * File:   CompileDesign.h
 * Author: alain
 *
 * Created on July 1, 2017, 1:11 PM
 */

#ifndef SURELOG_COMPILEDESIGN_H
#define SURELOG_COMPILEDESIGN_H
#pragma once

#include <Surelog/Design/Design.h>

// UHDM
#include <uhdm/containers.h>
#include <uhdm/sv_vpi_user.h>
#include <uhdm/uhdm_forward_decl.h>

#include <mutex>

namespace UHDM {
class Serializer;
}

namespace SURELOG {

class Compiler;
class SymbolTable;
class ValuedComponentI;

void decompile(ValuedComponentI* instance);

class CompileDesign {
 public:
  // Note: takes owernship of compiler
  explicit CompileDesign(Compiler* compiler);
  virtual ~CompileDesign();  // Used in MockCompileDesign

  bool compile();
  bool elaborate();
  void purgeParsers();
  bool writeUHDM(PathId fileId);

  UHDM::Serializer& getSerializer();
  void lockSerializer();
  void unlockSerializer();

  Compiler* getCompiler() const { return m_compiler; }
  UHDM::VectorOfinclude_file_info* getFileInfo() { return m_fileInfo; }
  std::map<const UHDM::typespec*, const UHDM::typespec*>& getSwapedObjects() {
    return m_typespecSwapMap;
  }

 private:
  CompileDesign(const CompileDesign& orig) = delete;

  template <class ObjectType, class ObjectMapType, typename FunctorType>
  void compileMT_(ObjectMapType& objects, int32_t maxThreadCount);

  void collectObjects_(Design::FileIdDesignContentMap& all_files,
                       Design* design, bool finalCollection);
  bool compilation_();
  bool elaboration_();

  Compiler* const m_compiler;
  std::vector<SymbolTable*> m_symbolTables;
  std::vector<ErrorContainer*> m_errorContainers;
  UHDM::VectorOfinclude_file_info* m_fileInfo = nullptr;
  std::map<const UHDM::typespec*, const UHDM::typespec*> m_typespecSwapMap;
};

}  // namespace SURELOG

#endif /* SURELOG_COMPILEDESIGN_H */
