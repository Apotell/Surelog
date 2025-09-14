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
 * File:   CompileFileContent.cpp
 * Author: alain
 *
 * Created on March 28, 2018, 10:16 PM
 */

#include "Surelog/DesignCompile/CompileFileContent.h"

#include <uhdm/design.h>

#include <cstdint>
#include <stack>
#include <vector>

#include "Surelog/Common/NodeId.h"
#include "Surelog/Design/FileCNodeId.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/Design/Signal.h"
#include "Surelog/Design/VObject.h"
#include "Surelog/DesignCompile/CompileDesign.h"
#include "Surelog/DesignCompile/CompileHelper.h"
#include "Surelog/SourceCompile/Compiler.h"
#include "Surelog/SourceCompile/VObjectTypes.h"

namespace SURELOG {

int32_t FunctorCompileFileContentDecl::operator()() const {
  CompileFileContent* instance = new CompileFileContent(
      m_session, m_compileDesign, m_fileContent, m_design, true);
  instance->compile(Elaborate::No, Reduce::No);
  delete instance;
  return 0;
}

int32_t FunctorCompileFileContent::operator()() const {
  CompileFileContent* instance = new CompileFileContent(
      m_session, m_compileDesign, m_fileContent, m_design, false);
  instance->compile(Elaborate::No, Reduce::No);
  delete instance;
  return 0;
}

bool CompileFileContent::compile(Elaborate elaborate, Reduce reduce) {
  m_helper.setElaborate(elaborate);
  m_helper.setReduce(reduce);
  return collectObjects_();
}

bool CompileFileContent::collectObjects_() {
  std::vector<VObjectType> stopPoints = {
      VObjectType::paModule_declaration,
      VObjectType::paInterface_declaration,
      VObjectType::paProgram_declaration,
      VObjectType::paClass_declaration,
      VObjectType::PRIMITIVE,
      VObjectType::paPackage_declaration,
      VObjectType::paFunction_declaration,
      VObjectType::paInterface_class_declaration};

  Design* const design = m_compileDesign->getCompiler()->getDesign();
  uhdm::Design* const udesign = design->getUhdmDesign();
  const uhdm::ScopedScope scopedScope(udesign);

  FileContent* fC = m_fileContent;
  if (fC->getSize() == 0) return true;
  const VObject& current = fC->Object(NodeId(fC->getSize() - 2));
  NodeId id = current.m_child;
  if (!id) id = current.m_sibling;
  if (!id) return false;
  std::stack<NodeId> stack;
  stack.push(id);
  while (!stack.empty()) {
    id = stack.top();
    stack.pop();
    const VObject& current = fC->Object(id);
    VObjectType type = fC->Type(id);
    switch (type) {
      case VObjectType::paPackage_import_item: {
        if (m_declOnly == false) {
          m_helper.importPackage(m_fileContent, m_design, fC, id,
                                 m_compileDesign);
          m_helper.compileImportDeclaration(m_fileContent, fC, id,
                                            m_compileDesign);
          FileCNodeId fnid(fC, id);
          m_fileContent->addObject(type, fnid);
        }
        break;
      }
      case VObjectType::paFunction_declaration: {
        if (m_declOnly == false) {
          m_helper.compileFunction(m_fileContent, fC, id, m_compileDesign,
                                   Reduce::No, nullptr, true);
          m_helper.compileFunction(m_fileContent, fC, id, m_compileDesign,
                                   Reduce::No, nullptr, true);
        }
        break;
      }
      case VObjectType::paData_declaration: {
        if (m_declOnly) {
          m_helper.compileDataDeclaration(m_fileContent, fC, id, false,
                                          m_compileDesign, Reduce::Yes,
                                          nullptr);
        }
        break;
      }
      case VObjectType::paBind_directive: {
        if (!m_declOnly) {
          m_helper.compileBindStmt(m_fileContent, fC, id, m_compileDesign,
                                   nullptr);
        }
        break;
      }
      case VObjectType::paParameter_declaration: {
        if (m_declOnly) {
          NodeId list_of_type_assignments = fC->Child(id);
          if (fC->Type(list_of_type_assignments) ==
                  VObjectType::paType_assignment_list ||
              fC->Type(list_of_type_assignments) == VObjectType::TYPE) {
            // Type param
            m_helper.compileParameterDeclaration(
                m_fileContent, fC, list_of_type_assignments, m_compileDesign,
                Reduce::Yes, false, nullptr, false, false);

          } else {
            m_helper.compileParameterDeclaration(m_fileContent, fC, id,
                                                 m_compileDesign, Reduce::Yes,
                                                 false, nullptr, false, false);
          }
        }
        break;
      }
      case VObjectType::paLet_declaration: {
        if (m_declOnly) {
          m_helper.compileLetDeclaration(m_fileContent, fC, id,
                                         m_compileDesign);
        }
        break;
      }
      case VObjectType::paLocal_parameter_declaration: {
        if (m_declOnly) {
          NodeId list_of_type_assignments = fC->Child(id);
          if (fC->Type(list_of_type_assignments) ==
                  VObjectType::paType_assignment_list ||
              fC->Type(list_of_type_assignments) == VObjectType::TYPE) {
            // Type param
            m_helper.compileParameterDeclaration(
                m_fileContent, fC, list_of_type_assignments, m_compileDesign,
                Reduce::Yes, true, nullptr, false, false);

          } else {
            m_helper.compileParameterDeclaration(m_fileContent, fC, id,
                                                 m_compileDesign, Reduce::Yes,
                                                 true, nullptr, false, false);
          }
        }
        break;
      }
      default:
        break;
    }

    if (current.m_sibling) stack.push(current.m_sibling);
    if (current.m_child) {
      if (!stopPoints.empty()) {
        bool stop = false;
        for (auto t : stopPoints) {
          if (t == current.m_type) {
            stop = true;
            break;
          }
        }
        if (!stop)
          if (current.m_child) stack.push(current.m_child);
      } else {
        if (current.m_child) stack.push(current.m_child);
      }
    }
  }

  for (Signal* sig : m_fileContent->getSignals()) {
    const FileContent* fC = sig->getFileContent();
    NodeId id = sig->getNodeId();
    // Assignment to a default value
    uhdm::Expr* exp = m_helper.exprFromAssign(
        m_fileContent, m_compileDesign, fC, id, sig->getUnpackedDimension());
    if (uhdm::Any* obj =
            m_helper.compileSignals(m_fileContent, m_compileDesign, sig)) {
      fC->populateCoreMembers(sig->getNameId(), sig->getNameId(), obj);
      obj->setParent(udesign);
      if (exp != nullptr) {
        exp->setParent(obj, true);
        if (uhdm::Variables* const var = any_cast<uhdm::Variables>(obj)) {
          var->setExpr(exp);
        }
      }
    }
  }

  return true;
}

}  // namespace SURELOG
