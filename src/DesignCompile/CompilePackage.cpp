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
 * File:   CompilePackage.cpp
 * Author: alain
 *
 * Created on March 22, 2018, 9:57 PM
 */

#include "Surelog/DesignCompile/CompilePackage.h"

#include "Surelog/CommandLine/CommandLineParser.h"
#include "Surelog/Common/NodeId.h"
#include "Surelog/Common/Session.h"
#include "Surelog/Design/FileCNodeId.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/Design/VObject.h"
#include "Surelog/DesignCompile/CompileDesign.h"
#include "Surelog/DesignCompile/CompileHelper.h"
#include "Surelog/ErrorReporting/Error.h"
#include "Surelog/ErrorReporting/ErrorContainer.h"
#include "Surelog/ErrorReporting/ErrorDefinition.h"
#include "Surelog/ErrorReporting/Location.h"
#include "Surelog/Package/Package.h"
#include "Surelog/SourceCompile/Compiler.h"
#include "Surelog/SourceCompile/SymbolTable.h"
#include "Surelog/SourceCompile/VObjectTypes.h"
#include "Surelog/Utils/StringUtils.h"

// UHDM
#include <uhdm/BaseClass.h>
#include <uhdm/Serializer.h>
#include <uhdm/containers.h>
#include <uhdm/package.h>
#include <uhdm/property_decl.h>
#include <uhdm/sequence_decl.h>

#include <cstdint>
#include <stack>
#include <string>
#include <vector>

namespace SURELOG {

int32_t FunctorCompilePackage::operator()() const {
  if (CompilePackage* instance =
          new CompilePackage(m_session, m_compileDesign, m_package, m_design)) {
    instance->compile();
    delete instance;
  }
  return 0;
}

bool CompilePackage::compile() {
  if (!m_package) return false;
  uhdm::Package* pack = m_package->getUhdmModel<uhdm::Package>();
  const FileContent* fC = m_package->m_fileContents[0];
  SymbolTable* const symbols = m_session->getSymbolTable();
  NodeId packId = m_package->m_nodeIds[0];
  fC->populateCoreMembers(packId, packId, pack);

  m_package->m_exprBuilder.setDesign(
      m_compileDesign->getCompiler()->getDesign());

  if (ErrorContainer* errors2 = new ErrorContainer(m_session)) {
    Location loc(fC->getFileId(packId), fC->Line(packId), fC->Column(packId),
                 symbols->getId(m_package->getName()));
    Error err(ErrorDefinition::COMP_COMPILE_PACKAGE, loc);

    errors2->addError(err);
    errors2->printMessage(err, m_session->getCommandLineParser()->muteStdout());
    delete errors2;
  }

  const uhdm::ScopedScope scopedScope(pack);
  collectObjects_(CollectType::FUNCTION);
  collectObjects_(CollectType::DEFINITION);
  m_helper.evalScheduledExprs(m_package);
  collectObjects_(CollectType::OTHER);

  do {
    packId = fC->Child(packId);
  } while (packId && (fC->Type(packId) != VObjectType::paAttribute_instance));
  if (packId) {
    if (uhdm::AttributeCollection* attributes =
            m_helper.compileAttributes(m_package, fC, packId, nullptr)) {
      m_package->setAttributes(attributes);
    }
  }

  return true;
}

bool CompilePackage::collectObjects_(CollectType collectType) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();
  std::vector<VObjectType> stopPoints = {
      VObjectType::paClass_declaration,
      VObjectType::paFunction_body_declaration,
      VObjectType::paTask_body_declaration,
      VObjectType::paInterface_class_declaration};
  m_helper.setDesign(m_compileDesign->getCompiler()->getDesign());
  for (uint32_t i = 0; i < m_package->m_fileContents.size(); i++) {
    const FileContent* fC = m_package->m_fileContents[i];

    NodeId id = fC->Child(m_package->m_nodeIds[i]);
    if (!id) id = fC->Sibling(m_package->m_nodeIds[i]);
    if (!id) continue;

    if (collectType == CollectType::FUNCTION) {
      // Package imports
      std::vector<FileCNodeId> pack_imports;
      // - Local file imports
      for (auto& import : fC->getObjects(VObjectType::paPackage_import_item)) {
        pack_imports.emplace_back(import);
      }

      for (auto& pack_import : pack_imports) {
        const FileContent* pack_fC = pack_import.fC;
        NodeId pack_id = pack_import.nodeId;
        m_helper.importPackage(m_package, m_design, pack_fC, pack_id, true);
      }
    }

    std::stack<NodeId> stack;
    stack.emplace(id);
    while (!stack.empty()) {
      id = stack.top();
      stack.pop();

      VObjectType type = fC->Type(id);
      switch (type) {
        case VObjectType::paPackage_import_item: {
          if (collectType != CollectType::FUNCTION) break;
          m_helper.importPackage(m_package, m_design, fC, id, true);
          m_helper.compileImportDeclaration(m_package, fC, id);
          break;
        }
        case VObjectType::paParameter_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          NodeId list_of_type_assignments = fC->Child(id);
          if (fC->Type(list_of_type_assignments) ==
                  VObjectType::paType_assignment_list ||
              fC->Type(list_of_type_assignments) == VObjectType::TYPE) {
            // Type param
            m_helper.compileParameterDeclaration(m_package, fC,
                                                 list_of_type_assignments,
                                                 false, nullptr, false, false);
          } else {
            m_helper.compileParameterDeclaration(m_package, fC, id, false,
                                                 nullptr, false, false);
          }
          break;
        }
        case VObjectType::paLocal_parameter_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          NodeId list_of_type_assignments = fC->Child(id);
          if (fC->Type(list_of_type_assignments) ==
                  VObjectType::paType_assignment_list ||
              fC->Type(list_of_type_assignments) == VObjectType::TYPE) {
            // Type param
            m_helper.compileParameterDeclaration(m_package, fC,
                                                 list_of_type_assignments, true,
                                                 nullptr, false, false);
          } else {
            m_helper.compileParameterDeclaration(m_package, fC, id, true,
                                                 nullptr, false, false);
          }
          break;
        }
        case VObjectType::paTask_declaration: {
          // Called twice, placeholder first, then definition
          if (collectType == CollectType::OTHER) break;
          m_helper.compileTask(m_package, fC, id, nullptr, false);
          break;
        }
        case VObjectType::paFunction_declaration: {
          // Called twice, placeholder first, then definition
          if (collectType == CollectType::OTHER) break;
          m_helper.compileFunction(m_package, fC, id, nullptr, false);
          break;
        }
        case VObjectType::paLet_declaration: {
          if (collectType != CollectType::FUNCTION) break;
          m_helper.compileLetDeclaration(m_package, fC, id);
          break;
        }
        case VObjectType::paParam_assignment: {
          if (collectType != CollectType::DEFINITION) break;
          FileCNodeId fnid(fC, id);
          m_package->addObject(type, fnid);
          break;
        }
        case VObjectType::paClass_declaration: {
          if (collectType != CollectType::OTHER) break;
          NodeId nameId = fC->Child(id);
          if (fC->Type(nameId) == VObjectType::VIRTUAL) {
            nameId = fC->Sibling(nameId);
          }
          const std::string_view name = fC->SymName(nameId);
          FileCNodeId fnid(fC, nameId);
          m_package->addObject(type, fnid);

          std::string completeName = StrCat(m_package->getName(), "::", name);

          DesignComponent* comp = fC->getComponentDefinition(completeName);

          m_package->addNamedObject(name, fnid, comp);
          break;
        }
        case VObjectType::paClass_constructor_declaration: {
          if (collectType != CollectType::OTHER) break;
          m_helper.compileClassConstructorDeclaration(m_package, fC, id);
          break;
        }
        case VObjectType::paNet_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          m_helper.compileNetDeclaration(m_package, fC, id, false,
                                         m_attributes);
          m_attributes = nullptr;
          break;
        }
        case VObjectType::paData_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          m_helper.compileDataDeclaration(m_package, fC, id, false,
                                          m_attributes);
          m_attributes = nullptr;
          break;
        }
        case VObjectType::paAttribute_instance: {
          if (collectType != CollectType::DEFINITION) break;
          m_attributes = m_helper.compileAttributes(m_package, fC, id, nullptr);
          break;
        }
        case VObjectType::paProperty_declaration: {
          if (collectType != CollectType::OTHER) break;
          uhdm::PropertyDecl* decl = m_helper.compilePropertyDeclaration(
              m_package, fC, id, nullptr, nullptr);
          m_package->addPropertyDecl(decl);
          break;
        }
        case VObjectType::paSequence_declaration: {
          if (collectType != CollectType::OTHER) break;
          uhdm::SequenceDecl* decl = m_helper.compileSequenceDeclaration(
              m_package, fC, id, nullptr, nullptr);
          m_package->addSequenceDecl(decl);
          break;
        }
        case VObjectType::paDpi_import_export: {
          if (collectType != CollectType::FUNCTION) break;
          NodeId Import = fC->Child(id);
          NodeId StringLiteral = fC->Sibling(Import);
          NodeId Context_keyword = fC->Sibling(StringLiteral);
          NodeId Task_prototype;
          if (fC->Type(Context_keyword) == VObjectType::paContext_keyword)
            Task_prototype = fC->Sibling(Context_keyword);
          else
            Task_prototype = Context_keyword;
          if (fC->Type(Task_prototype) == VObjectType::paTask_prototype) {
            Task* task = m_helper.compileTaskPrototype(m_package, fC, id);
            m_package->insertTask(task);
          } else {
            Function* func =
                m_helper.compileFunctionPrototype(m_package, fC, id);
            m_package->insertFunction(func);
          }
          break;
        }
        case VObjectType::STRING_CONST: {
          if (collectType != CollectType::DEFINITION) break;
          NodeId sibling = fC->Sibling(id);
          if (!sibling) {
            if (fC->Type(fC->Parent(id)) != VObjectType::paPackage_declaration)
              break;
            const std::string_view endLabel = fC->SymName(id);
            m_package->setEndLabel(endLabel);
            std::string_view moduleName =
                StringUtils::ltrim_until(m_package->getName(), '@');
            if (endLabel != moduleName) {
              Location loc(fC->getFileId(m_package->getNodeIds()[0]),
                           fC->Line(m_package->getNodeIds()[0]),
                           fC->Column(m_package->getNodeIds()[0]),
                           symbols->registerSymbol(moduleName));
              Location loc2(fC->getFileId(id), fC->Line(id), fC->Column(id),
                            symbols->registerSymbol(endLabel));
              Error err(ErrorDefinition::COMP_UNMATCHED_LABEL, loc, loc2);
              errors->addError(err);
            }
          }
          break;
        }
        default:
          break;
      }

      if (NodeId siblingId = fC->Sibling(id)) stack.emplace(siblingId);
      if (std::find(stopPoints.cbegin(), stopPoints.cend(), type) ==
          stopPoints.cend()) {
        if (NodeId childId = fC->Child(id)) {
          stack.emplace(childId);
        }
      }
    }
  }
  return true;
}

}  // namespace SURELOG
