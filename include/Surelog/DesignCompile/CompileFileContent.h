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
 * File:   CompileFileContent.h
 * Author: alain
 *
 * Created on March 28, 2018, 10:16 PM
 */

#ifndef SURELOG_COMPILEFILECONTENT_H
#define SURELOG_COMPILEFILECONTENT_H
#pragma once

#include <Surelog/DesignCompile/CompileHelper.h>

namespace SURELOG {
class CompileDesign;
class Design;
class FileContent;
class Session;

struct FunctorCompileFileContentDecl {
  FunctorCompileFileContentDecl(Session* session, CompileDesign* compiler,
                                FileContent* file, Design* design)
      : m_session(session),
        m_compileDesign(compiler),
        m_fileContent(file),
        m_design(design) {}
  int32_t operator()() const;

 private:
  Session* const m_session = nullptr;
  CompileDesign* const m_compileDesign;
  FileContent* const m_fileContent;
  Design* const m_design;
};

struct FunctorCompileFileContent {
  FunctorCompileFileContent(Session* session, CompileDesign* compiler,
                            FileContent* file, Design* design)
      : m_session(session),
        m_compileDesign(compiler),
        m_fileContent(file),
        m_design(design) {}
  int32_t operator()() const;

 private:
  Session* const m_session = nullptr;
  CompileDesign* const m_compileDesign;
  FileContent* const m_fileContent;
  Design* const m_design;
};

class CompileFileContent final {
 public:
  CompileFileContent(Session* session, CompileDesign* compiler,
                     FileContent* file, Design* design, bool declOnly)
      : m_session(session),
        m_compileDesign(compiler),
        m_fileContent(file),
        m_design(design),
        m_helper(session),
        m_declOnly(declOnly) {}

  bool compile();

 private:
  CompileFileContent(const CompileFileContent&) = delete;

  Session* const m_session = nullptr;
  CompileDesign* const m_compileDesign;
  FileContent* const m_fileContent;
  Design* const m_design = nullptr;
  CompileHelper m_helper;
  bool m_declOnly = false;
};

}  // namespace SURELOG

#endif /* SURELOG_COMPILEFILECONTENT_H */
