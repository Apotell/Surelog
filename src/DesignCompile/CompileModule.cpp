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
 * File:   CompileModule.cpp
 * Author: alain
 *
 * Created on March 22, 2018, 9:43 PM
 */

#include "Surelog/DesignCompile/CompileModule.h"

#include "Surelog/CommandLine/CommandLineParser.h"
#include "Surelog/Common/FileSystem.h"
#include "Surelog/Common/NodeId.h"
#include "Surelog/Common/Session.h"
#include "Surelog/Common/SymbolId.h"
#include "Surelog/Design/ClockingBlock.h"
#include "Surelog/Design/FileCNodeId.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/Design/ModuleDefinition.h"
#include "Surelog/Design/ModuleInstance.h"
#include "Surelog/Design/Signal.h"
#include "Surelog/Design/VObject.h"
#include "Surelog/Design/ValuedComponentI.h"
#include "Surelog/DesignCompile/CompileDesign.h"
#include "Surelog/DesignCompile/CompileHelper.h"
#include "Surelog/ErrorReporting/Error.h"
#include "Surelog/ErrorReporting/ErrorContainer.h"
#include "Surelog/ErrorReporting/ErrorDefinition.h"
#include "Surelog/ErrorReporting/Location.h"
#include "Surelog/Library/Library.h"
#include "Surelog/SourceCompile/Compiler.h"
#include "Surelog/SourceCompile/SymbolTable.h"
#include "Surelog/SourceCompile/VObjectTypes.h"
#include "Surelog/Utils/StringUtils.h"

// UHDM
#include <uhdm/Serializer.h>
#include <uhdm/always.h>
#include <uhdm/assign_stmt.h>
#include <uhdm/assignment.h>
#include <uhdm/attribute.h>
#include <uhdm/clocking_block.h>
#include <uhdm/constant.h>
#include <uhdm/cont_assign.h>
#include <uhdm/containers.h>
#include <uhdm/final_stmt.h>
#include <uhdm/initial.h>
#include <uhdm/io_decl.h>
#include <uhdm/logic_net.h>
#include <uhdm/module.h>
#include <uhdm/module_array.h>
#include <uhdm/property_decl.h>
#include <uhdm/ref_module.h>
#include <uhdm/ref_obj.h>
#include <uhdm/sequence_decl.h>
#include <uhdm/table_entry.h>
#include <uhdm/udp_defn.h>
#include <uhdm/vpi_user.h>

#include <cstdint>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SURELOG {
int32_t FunctorCompileModule::operator()() const {
  if (CompileModule* instance = new CompileModule(
          m_session, m_compileDesign, m_module, m_design, m_instance)) {
    instance->compile(Elaborate::No, Reduce::No);
    delete instance;
  }

  // if (m_compileDesign->getCompiler()->getCommandLineParser()->elaborate()) {
  //   if (CompileModule* instance = new CompileModule(
  //           m_session, m_compileDesign, m_module, m_design, m_instance))) {
  //     instance->compile(Elaborate::Yes, Reduce::Yes);
  //     delete instance;
  //   }
  // }

  return 0;
}

bool CompileModule::compile(Elaborate elaborate, Reduce reduce) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  CommandLineParser* const clp = m_session->getCommandLineParser();

  m_helper.setElaborate(elaborate);
  m_helper.setReduce(reduce);
  const FileContent* const fC = m_module->m_fileContents[0];
  NodeId nodeId = m_module->m_nodeIds[0];
  Location loc(fC->getFileId(nodeId), fC->Line(nodeId), fC->Column(nodeId),
               symbols->registerSymbol(m_module->getName()));
  VObjectType moduleType = fC->Type(nodeId);
  ErrorDefinition::ErrorType errType = ErrorDefinition::COMP_COMPILE_MODULE;
  switch (moduleType) {
    case VObjectType::paLoop_generate_construct:
    case VObjectType::paConditional_generate_construct:
    case VObjectType::paGenerate_begin_end_block:
    case VObjectType::paGenerate_item:
    case VObjectType::paGenerate_module_conditional_statement:
    case VObjectType::paGenerate_module_loop_statement:
    case VObjectType::paGenerate_module_block:
    case VObjectType::paGenerate_module_item:
    case VObjectType::paGenerate_module_named_block:
    case VObjectType::paGenerate_interface_conditional_statement:
    case VObjectType::paGenerate_interface_loop_statement:
    case VObjectType::paGenerate_interface_block:
    case VObjectType::paGenerate_interface_item:
    case VObjectType::paGenerate_interface_named_block:
    case VObjectType::paGenerate_region:
      errType = ErrorDefinition::COMP_COMPILE_GENERATE_BLOCK;
      break;
    case VObjectType::paInterface_declaration:
      errType = ErrorDefinition::COMP_COMPILE_INTERFACE;
      break;
    case VObjectType::paUdp_declaration:
      errType = ErrorDefinition::COMP_COMPILE_UDP;
      break;
    case VObjectType::paChecker_declaration:
      errType = ErrorDefinition::COMP_COMPILE_CHECKER;
      break;
    default:
      break;
  }

  m_module->setDesignElement(fC->getDesignElement(m_module->getName()));
  m_helper.setElaborate(elaborate);
  m_helper.setReduce(reduce);

  auto& blackboxModules = clp->getBlackBoxModules();
  bool skipModule = false;
  std::string libName;
  if (!m_module->getFileContents().empty())
    libName = m_module->getFileContents()[0]->getLibrary()->getName();
  const std::string_view modName = m_module->getName();
  if (blackboxModules.find(modName) != blackboxModules.end()) {
    errType = ErrorDefinition::COMP_SKIPPING_BLACKBOX_MODULE;
    skipModule = true;
  }
  auto& blackboxInstances = clp->getBlackBoxInstances();
  std::string instanceName;
  if (m_instance) {
    if (ModuleInstance* inst =
            valuedcomponenti_cast<ModuleInstance*>(m_instance)) {
      instanceName = inst->getFullPathName();
    }
  }
  if (blackboxInstances.find(instanceName) != blackboxInstances.end()) {
    errType = ErrorDefinition::COMP_SKIPPING_BLACKBOX_INSTANCE;
    skipModule = true;
  }
  if (blackboxInstances.find(modName) != blackboxInstances.end()) {
    errType = ErrorDefinition::COMP_SKIPPING_BLACKBOX_INSTANCE;
    skipModule = true;
  }

  Error err(errType, loc);
  ErrorContainer* errors = new ErrorContainer(m_session);
  errors->addError(err);
  errors->printMessage(err, clp->muteStdout());
  delete errors;

  if (skipModule) {
    return true;
  }

  switch (moduleType) {
    case VObjectType::paModule_declaration:
      if (!collectModuleObjects_(CollectType::FUNCTION)) return false;
      if (!collectModuleObjects_(CollectType::DEFINITION)) return false;
      if (!collectModuleObjects_(CollectType::GENERATE_REGIONS)) return false;
      if (!collectModuleObjects_(CollectType::OTHER)) return false;
      if (!checkModule_()) return false;
      break;
    case VObjectType::paLoop_generate_construct:
    case VObjectType::paConditional_generate_construct:
    case VObjectType::paGenerate_item:
    case VObjectType::paGenerate_begin_end_block:
    case VObjectType::paGenerate_module_conditional_statement:
    case VObjectType::paGenerate_module_loop_statement:
    case VObjectType::paGenerate_module_block:
    case VObjectType::paGenerate_module_item:
    case VObjectType::paGenerate_module_named_block:
    case VObjectType::paGenerate_region:
      if (!collectModuleObjects_(CollectType::FUNCTION)) return false;
      if (!collectModuleObjects_(CollectType::DEFINITION)) return false;
      if (!collectModuleObjects_(CollectType::OTHER)) return false;
      if (!checkModule_()) return false;
      break;
    case VObjectType::paInterface_declaration:
      if (!collectInterfaceObjects_(CollectType::FUNCTION)) return false;
      if (!collectInterfaceObjects_(CollectType::DEFINITION)) return false;
      if (!collectModuleObjects_(CollectType::GENERATE_REGIONS)) return false;
      if (!collectInterfaceObjects_(CollectType::OTHER)) return false;
      if (!checkInterface_()) return false;
      break;
    case VObjectType::paGenerate_interface_conditional_statement:
    case VObjectType::paGenerate_interface_loop_statement:
    case VObjectType::paGenerate_interface_block:
    case VObjectType::paGenerate_interface_item:
    case VObjectType::paGenerate_interface_named_block:
      if (!collectInterfaceObjects_(CollectType::FUNCTION)) return false;
      if (!collectInterfaceObjects_(CollectType::DEFINITION)) return false;
      if (!collectInterfaceObjects_(CollectType::OTHER)) return false;
      if (!checkInterface_()) return false;
      break;
    case VObjectType::paUdp_declaration:
      if (!collectUdpObjects_()) return false;
      break;
    case VObjectType::paChecker_declaration:
      break;
    default:
      break;
  }

  switch (moduleType) {
    case VObjectType::paModule_declaration:
    case VObjectType::paInterface_declaration:
    case VObjectType::paUdp_declaration:
      do {
        VObject current = fC->Object(nodeId);
        nodeId = current.m_child;
      } while (nodeId &&
               (fC->Type(nodeId) != VObjectType::paAttribute_instance));
      if (nodeId) {
        if (uhdm::AttributeCollection* attributes = m_helper.compileAttributes(
                m_module, fC, nodeId, m_compileDesign, nullptr)) {
          m_module->setAttributes(attributes);
        }
      }

      break;
    default:
      break;
  }

  for (Signal* sig : m_module->getSignals()) {
    m_helper.compileSignal(m_module, m_compileDesign, sig, sig->getName(), true,
                           reduce);
  }

  return true;
}

bool CompileModule::collectUdpObjects_() {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  const FileContent* const fC = m_module->m_fileContents[0];
  NodeId id = m_module->m_nodeIds[0];
  VObject current = fC->Object(id);
  std::stack<NodeId> stack;
  stack.push(id);

  const uhdm::ScopedScope scopedScope(m_module->getUhdmModel());
  uhdm::UdpDefn* defn = m_module->getUhdmModel<uhdm::UdpDefn>();
  while (!stack.empty()) {
    id = stack.top();
    stack.pop();
    current = fC->Object(id);
    VObjectType type = fC->Type(id);
    switch (type) {
      case VObjectType::paUdp_declaration:
      case VObjectType::paUdp_nonansi_declaration:
      case VObjectType::paUdp_ansi_declaration: {
        NodeId Attributes = fC->Child(id);
        if (fC->Type(Attributes) == VObjectType::paAttribute_instance) {
          if (uhdm::AttributeCollection* attributes =
                  m_helper.compileAttributes(m_module, fC, Attributes,
                                             m_compileDesign, defn)) {
            defn->setAttributes(attributes);
          }
        }
        break;
      }
      case VObjectType::paUdp_port_list: {
        NodeId port = fC->Child(id);
        while (port) {
          uhdm::IODecl* io = s.make<uhdm::IODecl>();
          const std::string_view name = fC->SymName(port);
          fC->populateCoreMembers(port, port, io);
          io->setName(name);
          io->setParent(defn);
          port = fC->Sibling(port);
        }
        break;
      }
      case VObjectType::paUdp_output_declaration:
      case VObjectType::paUdp_reg_declaration: {
        NodeId Output = fC->Child(id);
        uhdm::LogicNet* net = s.make<uhdm::LogicNet>();
        if (fC->Type(Output) == VObjectType::paAttribute_instance) {
          if (uhdm::AttributeCollection* attributes =
                  m_helper.compileAttributes(m_module, fC, Output,
                                             m_compileDesign, net)) {
            net->setAttributes(attributes);
          }
          while (fC->Type(Output) == VObjectType::paAttribute_instance)
            Output = fC->Sibling(Output);
        }

        const std::string_view outputname = fC->SymName(Output);
        fC->populateCoreMembers(Output, Output, net);
        if (std::vector<uhdm::IODecl*>* ios = defn->getIODecls()) {
          for (auto io : *ios) {
            if (io->getName() == outputname) {
              if (io->getExpr() == nullptr) {
                io->setExpr(net);  // reg def do not override output def
                net->setParent(io);
              }
              net->setName(io->getName());
              io->setDirection(vpiOutput);
              break;
            }
          }
        }
        net->setParent(defn);
        break;
      }
      case VObjectType::paUdp_input_declaration: {
        NodeId Indentifier_list = fC->Child(id);
        uhdm::AttributeCollection* attributes = nullptr;
        if (fC->Type(Indentifier_list) == VObjectType::paAttribute_instance) {
          attributes = m_helper.compileAttributes(
              m_module, fC, Indentifier_list, m_compileDesign, nullptr);
          while (fC->Type(Indentifier_list) ==
                 VObjectType::paAttribute_instance)
            Indentifier_list = fC->Sibling(Indentifier_list);
        }
        NodeId Identifier = fC->Child(Indentifier_list);
        while (Identifier) {
          const std::string_view inputname = fC->SymName(Identifier);
          if (std::vector<uhdm::IODecl*>* ios = defn->getIODecls()) {
            uhdm::LogicNet* net = s.make<uhdm::LogicNet>();
            fC->populateCoreMembers(Identifier, Identifier, net);
            if (attributes != nullptr) {
              net->setAttributes(attributes);
              for (auto a : *attributes) a->setParent(net);
            }
            for (auto io : *ios) {
              if (io->getName() == inputname) {
                io->setExpr(net);
                net->setParent(io);
                net->setName(io->getName());
                io->setDirection(vpiInput);
                break;
              }
            }
            net->setParent(defn);
          }
          Identifier = fC->Sibling(Identifier);
        }
        break;
      }
      case VObjectType::paCombinational_entry: {
        NodeId Level_input_list = fC->Child(id);
        NodeId Output_symbol = fC->Sibling(Level_input_list);
        NodeId Level_symbol = fC->Child(Level_input_list);
        std::string ventry = "STRING:";
        uint32_t nb = 0;
        while (Level_symbol) {
          NodeId Symbol = fC->Child(Level_symbol);
          uint32_t nbSymb = 0;
          if (fC->Type(Symbol) == VObjectType::QMARK) {
            ventry += "? ";
            nbSymb = 1;
          } else if (fC->Type(Symbol) == VObjectType::paBinOp_Mult) {
            ventry += "* ";
            nbSymb = 1;
          } else {
            const std::string_view symb = fC->SymName(Symbol);
            nbSymb = symb.size();
            std::string symbols;
            for (uint32_t i = 0; i < nbSymb; i++) {
              char s = symb[i];
              symbols += s + std::string(" ");
            }
            ventry += symbols;
          }
          Level_symbol = fC->Sibling(Level_symbol);
          nb = nb + nbSymb;
        }
        ventry += ": ";
        NodeId Symbol = fC->Child(Output_symbol);
        ventry += fC->SymName(Symbol);
        uhdm::TableEntry* entry = s.make<uhdm::TableEntry>();
        entry->setParent(defn);
        entry->setValue(ventry);
        entry->setSize(nb);
        fC->populateCoreMembers(Level_input_list, Level_input_list, entry);
        break;
      }
      case VObjectType::paSequential_entry: {
        NodeId Seq_input_list = fC->Child(id);
        NodeId Level_input_list = fC->Child(Seq_input_list);
        NodeId Current_state = fC->Sibling(Seq_input_list);
        NodeId Next_state = fC->Sibling(Current_state);
        std::string ventry = "STRING:";
        uint32_t nb = 0;
        NodeId Level_symbol = fC->Child(Level_input_list);
        while (Level_symbol) {
          if (fC->Type(Level_symbol) == VObjectType::paEdge_indicator) {
            NodeId Level_Symbol = fC->Child(Level_symbol);
            while (Level_Symbol) {
              NodeId Symbol = fC->Child(Level_Symbol);
              if (fC->Type(Symbol) == VObjectType::QMARK) {
                ventry += "?";
              } else if (fC->Type(Symbol) == VObjectType::paBinOp_Mult) {
                ventry += "* ";
              } else {
                const std::string_view symb = fC->SymName(Symbol);
                ventry += symb;
              }
              Level_Symbol = fC->Sibling(Level_Symbol);
            }
            ventry += " ";
            nb++;
          } else {
            NodeId Symbol = fC->Child(Level_symbol);

            uint32_t nbSymb = 0;
            if (fC->Type(Symbol) == VObjectType::QMARK) {
              ventry += "? ";
              nbSymb = 1;
            } else if (fC->Type(Symbol) == VObjectType::paBinOp_Mult) {
              ventry += "* ";
              nbSymb = 1;
            } else {
              const std::string_view symb = fC->SymName(Symbol);
              nbSymb = symb.size();
              std::string symbols;
              for (uint32_t i = 0; i < nbSymb; i++) {
                char s = symb[i];
                symbols += s + std::string(" ");
              }
              ventry += symbols;
            }
            nb = nb + nbSymb;
          }
          Level_symbol = fC->Sibling(Level_symbol);
        }
        ventry += ": ";
        NodeId Symbol = fC->Child(Current_state);

        if (fC->Type(Symbol) == VObjectType::QMARK) {
          ventry += "? ";
        } else if (fC->Type(Symbol) == VObjectType::paBinOp_Mult) {
          ventry += "* ";
        } else {
          const std::string_view symb = fC->SymName(Symbol);
          uint32_t nbSymb = symb.size();
          std::string symbols;
          for (uint32_t i = 0; i < nbSymb; i++) {
            char s = symb[i];
            symbols += s + std::string(" ");
          }
          ventry += symbols;
        }

        ventry += ": ";
        Symbol = fC->Child(Next_state);

        if (fC->Type(Symbol) == VObjectType::paOutput_symbol) {
          Symbol = fC->Child(Symbol);
          const std::string_view symb = fC->SymName(Symbol);
          uint32_t nbSymb = symb.size();
          std::string symbols;
          for (uint32_t i = 0; i < nbSymb; i++) {
            char s = symb[i];
            symbols += s + std::string(" ");
          }
          ventry += symbols;
        } else {
          ventry += "-";
        }

        uhdm::TableEntry* entry = s.make<uhdm::TableEntry>();
        entry->setParent(defn);
        entry->setValue(ventry);
        entry->setSize(nb);
        fC->populateCoreMembers(Level_input_list, Level_input_list, entry);
        break;
      }
      case VObjectType::paUdp_initial_statement: {
        NodeId Identifier = fC->Child(id);
        NodeId Value = fC->Sibling(Identifier);
        uhdm::Initial* init = s.make<uhdm::Initial>();
        fC->populateCoreMembers(id, id, init);
        init->setParent(defn);
        defn->setInitial(init);
        uhdm::Assignment* assign_stmt = s.make<uhdm::Assignment>();
        init->setStmt(assign_stmt);
        uhdm::RefObj* ref = s.make<uhdm::RefObj>();
        ref->setName(fC->SymName(Identifier));
        ref->setParent(assign_stmt);
        fC->populateCoreMembers(Identifier, Identifier, ref);
        assign_stmt->setLhs(ref);
        fC->populateCoreMembers(id, id, assign_stmt);
        assign_stmt->setParent(init);
        uhdm::Constant* c = s.make<uhdm::Constant>();
        assign_stmt->setRhs(c);
        std::string val = StrCat("UINT:", fC->SymName(Value));
        c->setValue(val);
        c->setDecompile(fC->SymName(Value));
        c->setSize(64);
        c->setConstType(vpiUIntConst);
        c->setParent(assign_stmt);
        fC->populateCoreMembers(Value, Value, c);
        break;
      }
      default:
        break;
    }
    if (current.m_sibling) stack.push(current.m_sibling);
    if (current.m_child) stack.push(current.m_child);
  }

  return true;
}

bool CompileModule::collectModuleObjects_(CollectType collectType) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();
  std::vector<VObjectType> stopPoints = {
      VObjectType::paConditional_generate_construct,
      VObjectType::paGenerate_module_conditional_statement,
      VObjectType::paLoop_generate_construct,
      VObjectType::paGenerate_module_loop_statement,
      VObjectType::paPar_block,
      VObjectType::paSeq_block,
      VObjectType::paModule_declaration,
      VObjectType::paClass_declaration,
      VObjectType::paFunction_body_declaration,
      VObjectType::paTask_body_declaration};
  if (collectType == CollectType::GENERATE_REGIONS) {
    if (m_instance != nullptr) {
      stopPoints.emplace_back(VObjectType::paGenerate_region);
    }
  } else {
    stopPoints.emplace_back(VObjectType::paGenerate_region);
  }

  const uhdm::ScopedScope scopedScope(m_module->getUhdmModel());
  for (uint32_t i = 0; i < m_module->m_fileContents.size(); i++) {
    const FileContent* fC = m_module->m_fileContents[i];
    VObject current = fC->Object(m_module->m_nodeIds[i]);
    NodeId id = current.m_child;

    NodeId endOfBlockId;
    if (m_module->getGenBlockId()) {
      id = m_module->getGenBlockId();
      endOfBlockId = id;
      while (endOfBlockId) {
        VObjectType type = fC->Type(endOfBlockId);
        if (type == VObjectType::END) break;
        if (type == VObjectType::ELSE) break;
        endOfBlockId = fC->Sibling(endOfBlockId);
      }
      if (!endOfBlockId) endOfBlockId = fC->Sibling(m_module->getGenBlockId());
    }
    if (!id) id = current.m_sibling;
    if (!id) return false;

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
        m_helper.importPackage(m_module, m_design, pack_fC, pack_id,
                               m_compileDesign);
      }
    }
    NodeId ParameterPortListId;
    std::stack<NodeId> stack;
    stack.push(id);
    VObjectType port_direction = VObjectType::NO_TYPE;
    NodeId startId = id;
    while (!stack.empty()) {
      id = stack.top();
      if (endOfBlockId && (id == endOfBlockId)) {
        break;
      }
      if (ParameterPortListId && (id == ParameterPortListId)) {
        ParameterPortListId = InvalidNodeId;
      }
      stack.pop();
      current = fC->Object(id);
      VObjectType type = fC->Type(id);
      bool skipChildren = false;
      switch (type) {
        case VObjectType::paPackage_import_item: {
          if (collectType != CollectType::FUNCTION) break;
          m_helper.importPackage(m_module, m_design, fC, id, m_compileDesign);
          m_helper.compileImportDeclaration(m_module, fC, id, m_compileDesign);
          break;
        }
        case VObjectType::paAnsi_port_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          m_helper.compileAnsiPortDeclaration(m_module, fC, id, m_compileDesign,
                                              port_direction);
          m_attributes = nullptr;
          break;
        }
        case VObjectType::paPort: {
          if (fC->Child(id)) {
            m_hasNonNullPort = true;
          }
          if (collectType == CollectType::FUNCTION) m_nbPorts++;
          if (collectType != CollectType::DEFINITION) break;
          m_helper.compilePortDeclaration(m_module, fC, id, m_compileDesign,
                                          port_direction,
                                          m_hasNonNullPort || (m_nbPorts > 1));
          m_attributes = nullptr;
          break;
        }
        case VObjectType::paElaboration_system_task: {
          if (collectType != CollectType::FUNCTION) break;
          m_helper.elaborationSystemTask(m_module, fC, id, m_compileDesign);
          break;
        }
        case VObjectType::paInput_declaration:
        case VObjectType::paOutput_declaration:
        case VObjectType::paInout_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          m_helper.compilePortDeclaration(m_module, fC, id, m_compileDesign,
                                          port_direction, m_hasNonNullPort);
          m_attributes = nullptr;
          break;
        }
        case VObjectType::paClocking_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          compileClockingBlock_(fC, id);
          break;
        }
        case VObjectType::paNet_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          m_helper.compileNetDeclaration(m_module, fC, id, false,
                                         m_compileDesign, m_attributes);
          m_attributes = nullptr;
          break;
        }
        case VObjectType::paData_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          m_helper.compileDataDeclaration(m_module, fC, id, false,
                                          m_compileDesign, Reduce::No,
                                          m_attributes);
          m_attributes = nullptr;
          break;
        }
        case VObjectType::paAttribute_instance: {
          if (collectType != CollectType::DEFINITION) break;
          m_attributes = m_helper.compileAttributes(m_module, fC, id,
                                                    m_compileDesign, nullptr);
          break;
        }
        case VObjectType::paGenerate_begin_end_block: {
          if (id != startId) skipChildren = true;
          break;
        }
        case VObjectType::paPort_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          m_helper.compilePortDeclaration(m_module, fC, id, m_compileDesign,
                                          port_direction, m_hasNonNullPort);
          m_attributes = nullptr;
          break;
        }
        case VObjectType::paContinuous_assign: {
          if (collectType != CollectType::OTHER) break;
          uhdm::ContAssignCollection assigns =
              m_helper.compileContinuousAssignment(
                  m_module, fC, fC->Child(id), m_compileDesign,
                  m_module->getUhdmModel(), m_instance);
          if (m_module->getContAssigns() == nullptr) {
            m_module->setContAssigns(m_compileDesign->getSerializer()
                                         .makeCollection<uhdm::ContAssign>());
          }
          for (auto assign : assigns) {
            m_module->getContAssigns()->emplace_back(assign);
          }
          break;
        }
        case VObjectType::paProperty_declaration: {
          if (collectType != CollectType::OTHER) break;
          if (uhdm::PropertyDecl* decl = m_helper.compilePropertyDeclaration(
                  m_module, fC, id, m_compileDesign, m_module->getUhdmModel(),
                  m_instance)) {
            m_module->addPropertyDecl(decl);
          }
          break;
        }
        case VObjectType::paSequence_declaration: {
          if (collectType != CollectType::OTHER) break;
          if (uhdm::SequenceDecl* decl = m_helper.compileSequenceDeclaration(
                  m_module, fC, id, m_compileDesign, m_module->getUhdmModel(),
                  m_instance)) {
            m_module->addSequenceDecl(decl);
          }
          break;
        }
        case VObjectType::paAlways_construct: {
          if (collectType != CollectType::OTHER) break;
          uhdm::Always* always =
              m_helper.compileAlwaysBlock(m_module, fC, id, m_compileDesign,
                                          m_module->getUhdmModel(), m_instance);
          uhdm::ProcessCollection* processes = m_module->getProcesses();
          if (processes == nullptr) {
            m_module->setProcesses(m_compileDesign->getSerializer()
                                       .makeCollection<uhdm::Process>());
            processes = m_module->getProcesses();
          }
          processes->emplace_back(always);
          break;
        }
        case VObjectType::paParameter_port_list: {
          if (collectType != CollectType::DEFINITION) break;
          ParameterPortListId = id;
          NodeId list_of_param_assignments = fC->Child(id);
          while (list_of_param_assignments) {
            m_helper.compileParameterDeclaration(
                m_module, fC, list_of_param_assignments, m_compileDesign,
                m_instance != nullptr ? Reduce::Yes : Reduce::No, false,
                m_instance, false, false);
            list_of_param_assignments = fC->Sibling(list_of_param_assignments);
          }
          break;
        }
        case VObjectType::paParameter_declaration: {
          if (collectType != CollectType::DEFINITION) break;

          NodeId list_of_type_assignments = fC->Child(id);
          if (fC->Type(list_of_type_assignments) ==
                  VObjectType::paType_assignment_list ||
              fC->Type(list_of_type_assignments) == VObjectType::TYPE) {
            // Type param
            m_helper.compileParameterDeclaration(
                m_module, fC, list_of_type_assignments, m_compileDesign,
                m_instance != nullptr ? Reduce::Yes : Reduce::No, false,
                m_instance, ParameterPortListId, false);

          } else {
            m_helper.compileParameterDeclaration(
                m_module, fC, id, m_compileDesign,
                m_instance != nullptr ? Reduce::Yes : Reduce::No, false,
                m_instance, ParameterPortListId, false);
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
            m_helper.compileParameterDeclaration(
                m_module, fC, list_of_type_assignments, m_compileDesign,
                m_instance != nullptr ? Reduce::Yes : Reduce::No, true,
                m_instance, ParameterPortListId, false);

          } else {
            m_helper.compileParameterDeclaration(
                m_module, fC, id, m_compileDesign,
                m_instance != nullptr ? Reduce::Yes : Reduce::No, true,
                m_instance, ParameterPortListId, false);
          }
          break;
        }
        case VObjectType::paTask_declaration: {
          // Called twice, placeholder first, then definition
          if (collectType == CollectType::OTHER) break;
          m_helper.compileTask(m_module, fC, id, m_compileDesign, Reduce::No,
                               m_instance, false);
          break;
        }
        case VObjectType::paFunction_declaration: {
          // Called twice, placeholder first, then definition
          if (collectType == CollectType::OTHER) break;
          m_helper.compileFunction(m_module, fC, id, m_compileDesign,
                                   Reduce::No, m_instance, false);
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
            Task* task = m_helper.compileTaskPrototype(m_module, fC, id,
                                                       m_compileDesign);
            m_module->insertTask(task);
          } else {
            Function* func = m_helper.compileFunctionPrototype(m_module, fC, id,
                                                               m_compileDesign);
            m_module->insertFunction(func);
          }
          break;
        }
        case VObjectType::paAssertion_item: {
          if (collectType != CollectType::OTHER) break;
          m_helper.compileAssertionItem(m_module, fC, id, m_compileDesign);
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
          m_module->addObject(type, fnid);

          std::string completeName = StrCat(m_module->getName(), "::", name);

          DesignComponent* comp = fC->getComponentDefinition(completeName);

          m_module->addNamedObject(name, fnid, comp);
          break;
        }
        case VObjectType::paClass_constructor_declaration: {
          if (collectType != CollectType::OTHER) break;
          m_helper.compileClassConstructorDeclaration(m_module, fC, id,
                                                      m_compileDesign);
          break;
        }
        case VObjectType::paBind_directive: {
          skipChildren = true;
          if (collectType != CollectType::OTHER) break;
          m_helper.compileBindStmt(m_module, fC, id, m_compileDesign,
                                   m_instance);
          break;
        }
        case VObjectType::paLet_declaration: {
          if (collectType != CollectType::FUNCTION) break;
          m_helper.compileLetDeclaration(m_module, fC, id, m_compileDesign);
          break;
        }
        case VObjectType::paParam_assignment:
        case VObjectType::paHierarchical_instance:
        case VObjectType::paUdp_instance:
        case VObjectType::paGate_instantiation:
        case VObjectType::paPar_block:
        case VObjectType::paSeq_block:
        case VObjectType::paDefparam_assignment: {
          if (collectType != CollectType::OTHER) break;
          FileCNodeId fnid(fC, id);
          m_module->addObject(type, fnid);
          break;
        }
        case VObjectType::paUdp_instantiation: {
          if (collectType != CollectType::OTHER) break;
          FileCNodeId fnid(fC, id);
          m_module->addObject(type, fnid);
          m_helper.compileUdpInstantiation(m_module, fC, m_compileDesign, id,
                                           m_instance);
          break;
        }
        case VObjectType::paN_input_gate_instance:
        case VObjectType::paN_output_gate_instance: {
          if (collectType != CollectType::OTHER) break;
          FileCNodeId fnid(fC, id);
          m_module->addObject(type, fnid);
          m_helper.compileGateInstantiation(m_module, fC, m_compileDesign, id,
                                            m_instance);
          break;
        }
        case VObjectType::paInterface_instantiation:
        case VObjectType::paModule_instantiation:
        case VObjectType::paProgram_instantiation: {
          if (collectType != CollectType::OTHER) break;
          std::pair<std::vector<uhdm::ModuleArray*>,
                    std::vector<uhdm::RefModule*>>
              result = m_helper.compileInstantiation(
                  m_module, fC, m_compileDesign, m_module->getUhdmModel(), id,
                  m_instance);
          if (!result.first.empty()) {
            auto subModuleArrays = m_module->getModuleArrays();
            if (subModuleArrays == nullptr) {
              subModuleArrays = m_compileDesign->getSerializer()
                                    .makeCollection<uhdm::ModuleArray>();
              m_module->setModuleArrays(subModuleArrays);
            }
            for (auto mod : result.first) {
              subModuleArrays->emplace_back(mod);
            }
          }
          if (!result.second.empty()) {
            auto subModules = m_module->getRefModules();
            if (subModules == nullptr) {
              subModules = m_compileDesign->getSerializer()
                               .makeCollection<uhdm::RefModule>();
              m_module->setRefModules(subModules);
            }
            for (auto mod : result.second) {
              subModules->emplace_back(mod);
            }
          }
          FileCNodeId fnid(fC, id);
          m_module->addObject(type, fnid);
          break;
        }
        case VObjectType::paInitial_construct: {
          if (collectType != CollectType::OTHER) break;
          uhdm::Initial* init = m_helper.compileInitialBlock(
              m_module, fC, id, m_compileDesign, m_module->getUhdmModel());
          uhdm::ProcessCollection* processes = m_module->getProcesses();
          if (processes == nullptr) {
            m_module->setProcesses(m_compileDesign->getSerializer()
                                       .makeCollection<uhdm::Process>());
            processes = m_module->getProcesses();
          }
          processes->emplace_back(init);
          break;
        }
        case VObjectType::paFinal_construct: {
          if (collectType != CollectType::OTHER) break;
          uhdm::FinalStmt* final = m_helper.compileFinalBlock(
              m_module, fC, id, m_compileDesign, m_module->getUhdmModel());
          uhdm::ProcessCollection* processes = m_module->getProcesses();
          if (processes == nullptr) {
            m_module->setProcesses(m_compileDesign->getSerializer()
                                       .makeCollection<uhdm::Process>());
            processes = m_module->getProcesses();
          }
          processes->emplace_back(final);
          break;
        }
        case VObjectType::STRING_CONST: {
          if (collectType != CollectType::DEFINITION) break;
          NodeId sibling = fC->Sibling(id);
          if (!sibling) {
            if (fC->Type(fC->Parent(id)) != VObjectType::paModule_declaration)
              break;
            const std::string_view endLabel = fC->SymName(id);
            m_module->setEndLabel(endLabel);
            std::string_view moduleName = m_module->getName();
            moduleName = StringUtils::ltrim_until(moduleName, '@');
            moduleName = StringUtils::ltrim_until(moduleName, ':');
            moduleName = StringUtils::ltrim_until(moduleName, ':');
            if (endLabel != moduleName) {
              Location loc(fC->getFileId(m_module->getNodeIds()[0]),
                           fC->Line(m_module->getNodeIds()[0]),
                           fC->Column(m_module->getNodeIds()[0]),
                           symbols->registerSymbol(moduleName));
              Location loc2(fC->getFileId(id), fC->Line(id), fC->Column(id),
                            symbols->registerSymbol(endLabel));
              errors->addError(ErrorDefinition::COMP_UNMATCHED_LABEL, loc,
                               loc2);
            }
          }
          break;
        }
        case VObjectType::paGenvar_declaration: {
          if (collectType != CollectType::OTHER) break;
          FileCNodeId fnid(fC, id);
          m_module->addObject(type, fnid);
          if (m_instance) break;
          if (uhdm::AnyCollection* vars =
                  m_helper.compileGenVars(m_module, fC, id, m_compileDesign)) {
            if (m_module->getGenVars() == nullptr) {
              m_module->setGenVars(
                  m_compileDesign->getSerializer().makeCollection<uhdm::Any>());
            }
            for (auto v : *vars) {
              m_module->getGenVars()->emplace_back(v);
            }
          }
          break;
        }
        case VObjectType::paGenerate_region:
        case VObjectType::paLoop_generate_construct:
        case VObjectType::paConditional_generate_construct:
        case VObjectType::paGenerate_module_conditional_statement:
        case VObjectType::paGenerate_module_loop_statement: {
          if (collectType != CollectType::OTHER) break;
          FileCNodeId fnid(fC, id);
          m_module->addObject(type, fnid);
          if (m_instance) break;
          if (uhdm::AnyCollection* stmts =
                  m_helper.compileGenStmt(m_module, fC, id, m_compileDesign)) {
            if (m_module->getGenStmts() == nullptr) {
              m_module->setGenStmts(
                  m_compileDesign->getSerializer().makeCollection<uhdm::Any>());
            }
            for (auto st : *stmts) {
              m_module->getGenStmts()->emplace_back(st);
            }
          }
          break;
        }
        default:
          break;
      }

      if (current.m_sibling) stack.push(current.m_sibling);
      if (current.m_child && (!skipChildren)) {
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
  }
  if (collectType == CollectType::DEFINITION) {
    for (Signal* port : m_module->getPorts()) {
      bool found = false;
      for (Signal* sig : m_module->getSignals()) {
        if (sig->getName() == port->getName()) {
          found = true;
          break;
        }
      }
      if (found == false) {
        m_module->addSignal(port);
      }
    }
  }

  return true;
}

bool CompileModule::collectInterfaceObjects_(CollectType collectType) {
  std::vector<VObjectType> stopPoints = {
      VObjectType::paConditional_generate_construct,
      VObjectType::paGenerate_module_conditional_statement,
      VObjectType::paGenerate_interface_conditional_statement,
      VObjectType::paLoop_generate_construct,
      VObjectType::paGenerate_module_loop_statement,
      VObjectType::paGenerate_interface_loop_statement,
      VObjectType::paPar_block,
      VObjectType::paSeq_block,
      VObjectType::paModule_declaration,
      VObjectType::paInterface_declaration,
      VObjectType::paClass_declaration,
      VObjectType::paFunction_body_declaration,
      VObjectType::paTask_body_declaration};
  if (collectType == CollectType::GENERATE_REGIONS) {
    if (m_instance != nullptr) {
      stopPoints.emplace_back(VObjectType::paGenerate_region);
    }
  } else {
    stopPoints.emplace_back(VObjectType::paGenerate_region);
  }

  const uhdm::ScopedScope scopedScope(m_module->getUhdmModel());
  for (uint32_t i = 0; i < m_module->m_fileContents.size(); i++) {
    const FileContent* fC = m_module->m_fileContents[i];
    VObject current = fC->Object(m_module->m_nodeIds[i]);
    NodeId id = current.m_child;
    if (!id) id = current.m_sibling;
    if (!id) return false;

    if (collectType == CollectType::FUNCTION) {
      // Package imports
      std::vector<FileCNodeId> pack_imports;
      // - Local file imports
      for (auto import : fC->getObjects(VObjectType::paPackage_import_item)) {
        pack_imports.emplace_back(import);
      }

      for (auto& pack_import : pack_imports) {
        const FileContent* pack_fC = pack_import.fC;
        NodeId pack_id = pack_import.nodeId;
        m_helper.importPackage(m_module, m_design, pack_fC, pack_id,
                               m_compileDesign);
      }
    }

    SymbolTable* const symbols = m_session->getSymbolTable();
    ErrorContainer* const errors = m_session->getErrorContainer();

    NodeId ParameterPortListId;
    std::stack<NodeId> stack;
    stack.push(id);
    VObjectType port_direction = VObjectType::NO_TYPE;
    NodeId startId = id;
    while (!stack.empty()) {
      id = stack.top();
      if (ParameterPortListId && (id == ParameterPortListId)) {
        ParameterPortListId = InvalidNodeId;
      }
      stack.pop();
      current = fC->Object(id);
      VObjectType type = fC->Type(id);
      bool skipChildren = false;
      switch (type) {
        case VObjectType::paPackage_import_item: {
          if (collectType != CollectType::FUNCTION) break;
          m_helper.importPackage(m_module, m_design, fC, id, m_compileDesign);
          m_helper.compileImportDeclaration(m_module, fC, id, m_compileDesign);
          break;
        }
        case VObjectType::paParameter_port_list: {
          if (collectType != CollectType::DEFINITION) break;
          ParameterPortListId = id;
          NodeId list_of_param_assignments = fC->Child(id);
          while (list_of_param_assignments) {
            m_helper.compileParameterDeclaration(
                m_module, fC, list_of_param_assignments, m_compileDesign,
                m_instance != nullptr ? Reduce::Yes : Reduce::No, false,
                m_instance, false, false);
            list_of_param_assignments = fC->Sibling(list_of_param_assignments);
          }
          break;
        }
        case VObjectType::paPort_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          m_helper.compilePortDeclaration(m_module, fC, id, m_compileDesign,
                                          port_direction, m_hasNonNullPort);
          m_attributes = nullptr;
          break;
        }
        case VObjectType::paAnsi_port_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          m_helper.compileAnsiPortDeclaration(m_module, fC, id, m_compileDesign,
                                              port_direction);
          m_attributes = nullptr;
          break;
        }
        case VObjectType::paNet_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          m_helper.compileNetDeclaration(m_module, fC, id, true,
                                         m_compileDesign, m_attributes);
          m_attributes = nullptr;
          break;
        }
        case VObjectType::paGenerate_begin_end_block: {
          if (id != startId) skipChildren = true;
          break;
        }
        case VObjectType::paData_declaration: {
          if (collectType != CollectType::DEFINITION) break;
          m_helper.compileDataDeclaration(m_module, fC, id, true,
                                          m_compileDesign, Reduce::No,
                                          m_attributes);
          m_attributes = nullptr;
          break;
        }
        case VObjectType::paAttribute_instance: {
          if (collectType != CollectType::DEFINITION) break;
          m_attributes = m_helper.compileAttributes(m_module, fC, id,
                                                    m_compileDesign, nullptr);
          break;
        }
        case VObjectType::paContinuous_assign: {
          if (collectType != CollectType::OTHER) break;
          uhdm::ContAssignCollection assigns =
              m_helper.compileContinuousAssignment(m_module, fC, fC->Child(id),
                                                   m_compileDesign, nullptr,
                                                   m_instance);
          if (m_module->getContAssigns() == nullptr) {
            m_module->setContAssigns(m_compileDesign->getSerializer()
                                         .makeCollection<uhdm::ContAssign>());
          }
          for (auto assign : assigns) {
            m_module->getContAssigns()->emplace_back(assign);
          }
          break;
        }
        case VObjectType::paAlways_construct: {
          if (collectType != CollectType::OTHER) break;
          uhdm::Always* always =
              m_helper.compileAlwaysBlock(m_module, fC, id, m_compileDesign,
                                          m_module->getUhdmModel(), m_instance);
          uhdm::ProcessCollection* processes = m_module->getProcesses();
          if (processes == nullptr) {
            m_module->setProcesses(m_compileDesign->getSerializer()
                                       .makeCollection<uhdm::Process>());
            processes = m_module->getProcesses();
          }
          processes->emplace_back(always);
          break;
        }
        case VObjectType::paTask_declaration: {
          if (collectType != CollectType::FUNCTION) break;
          m_helper.compileTask(m_module, fC, id, m_compileDesign, Reduce::No,
                               m_instance, false);
          break;
        }
        case VObjectType::paFunction_declaration: {
          if (collectType != CollectType::FUNCTION) break;
          m_helper.compileFunction(m_module, fC, id, m_compileDesign,
                                   Reduce::No, m_instance, false);
          break;
        }
        case VObjectType::paDpi_import_export: {
          if (collectType != CollectType::FUNCTION) break;
          Function* func = m_helper.compileFunctionPrototype(m_module, fC, id,
                                                             m_compileDesign);
          m_module->insertFunction(func);
          break;
        }
        case VObjectType::paAssertion_item: {
          if (collectType != CollectType::OTHER) break;
          m_helper.compileAssertionItem(m_module, fC, id, m_compileDesign);
          break;
        }
        case VObjectType::paElaboration_system_task: {
          if (collectType != CollectType::FUNCTION) break;
          m_helper.elaborationSystemTask(m_module, fC, id, m_compileDesign);
          break;
        }
        case VObjectType::paInterface_instantiation:
        case VObjectType::paModule_instantiation:
        case VObjectType::paProgram_instantiation: {
          if (collectType != CollectType::OTHER) break;
          std::pair<std::vector<uhdm::ModuleArray*>,
                    std::vector<uhdm::RefModule*>>
              result = m_helper.compileInstantiation(
                  m_module, fC, m_compileDesign, m_module->getUhdmModel(), id,
                  m_instance);
          if (!result.first.empty()) {
            auto subModuleArrays = m_module->getModuleArrays();
            if (subModuleArrays == nullptr) {
              subModuleArrays = m_compileDesign->getSerializer()
                                    .makeCollection<uhdm::ModuleArray>();
              m_module->setModuleArrays(subModuleArrays);
            }
            for (auto mod : result.first) {
              subModuleArrays->emplace_back(mod);
            }
          }
          if (!result.second.empty()) {
            auto subModules = m_module->getRefModules();
            if (subModules == nullptr) {
              subModules = m_compileDesign->getSerializer()
                               .makeCollection<uhdm::RefModule>();
              m_module->setRefModules(subModules);
            }
            for (auto mod : result.second) {
              subModules->emplace_back(mod);
            }
          }
          FileCNodeId fnid(fC, id);
          m_module->addObject(type, fnid);
          break;
        }
        case VObjectType::paClocking_declaration:
          if (collectType != CollectType::OTHER) break;
          compileClockingBlock_(fC, id);
          break;
        case VObjectType::paGenerate_interface_item: {
          if (collectType != CollectType::OTHER) break;
          // TODO: rewrite this rough implementation
          VObjectTypeUnorderedSet types = {VObjectType::paModport_item};
          std::vector<NodeId> items = fC->sl_collect_all(id, types);
          for (auto nodeId : items) {
            Location loc(fC->getFileId(nodeId), fC->Line(nodeId),
                         fC->Column(nodeId));
            errors->addError(ErrorDefinition::COMP_NO_MODPORT_IN_GENERATE, loc);
          }
          break;
        }
        case VObjectType::paProperty_declaration: {
          if (collectType != CollectType::OTHER) break;
          uhdm::PropertyDecl* decl = m_helper.compilePropertyDeclaration(
              m_module, fC, id, m_compileDesign, nullptr, m_instance);
          m_module->addPropertyDecl(decl);
          break;
        }
        case VObjectType::paSequence_declaration: {
          if (collectType != CollectType::OTHER) break;
          uhdm::SequenceDecl* decl = m_helper.compileSequenceDeclaration(
              m_module, fC, id, m_compileDesign, nullptr, m_instance);
          m_module->addSequenceDecl(decl);
          break;
        }
        case VObjectType::paModport_item:
          if (collectType != CollectType::OTHER) break;
          /*
           n<tb> u<45> t<StringConst> p<56> s<50> l<43>
           n<> u<46> t<PortDir_Inp> p<49> s<48> l<43>
           n<clk> u<47> t<StringConst> p<48> l<43>
           n<> u<48> t<Modport_simple_port> p<49> c<47> l<43>
           n<> u<49> t<Modport_simple_ports_declaration> p<50> c<46> l<43>
           n<> u<50> t<Modport_ports_declaration> p<56> c<49> s<55> l<43>
           n<> u<51> t<PortDir_Out> p<54> s<53> l<43>
           n<reset> u<52> t<StringConst> p<53> l<43>
           n<> u<53> t<Modport_simple_port> p<54> c<52> l<43>
           n<> u<54> t<Modport_simple_ports_declaration> p<55> c<51> l<43>
           n<> u<55> t<Modport_ports_declaration> p<56> c<54> l<43>
           n<> u<56> t<Modport_item> p<57> c<45> l<43>
           */
          {
            NodeId modportname = fC->Child(id);
            const std::string_view modportsymb = fC->SymName(modportname);
            NodeId modport_ports_declaration = fC->Sibling(modportname);
            VObjectType port_direction_type = VObjectType::NO_TYPE;
            while (modport_ports_declaration) {
              NodeId port_declaration = fC->Child(modport_ports_declaration);
              VObjectType port_declaration_type = fC->Type(port_declaration);
              if (port_declaration_type ==
                  VObjectType::paModport_simple_ports_declaration) {
                NodeId port_direction = fC->Child(port_declaration);
                port_direction_type = fC->Type(port_direction);
                NodeId modport_simple_port = fC->Sibling(port_direction);
                while (modport_simple_port) {
                  NodeId simple_port_name = fC->Child(modport_simple_port);
                  SymbolId port_symbol = fC->Name(simple_port_name);
                  bool port_exists = false;
                  for (auto& port : m_module->m_signals) {
                    if (port->getFileContent()->Name(port->getNameId()) ==
                        port_symbol) {
                      port_exists = true;
                      break;
                    }
                  }
                  NodeId Expression = fC->Sibling(simple_port_name);
                  if (!Expression) {
                    // If expression is not null, we cannot conclude here
                    if (!port_exists) {
                      Location loc(fC->getFileId(simple_port_name),
                                   fC->Line(simple_port_name),
                                   fC->Column(simple_port_name),
                                   symbols->registerSymbol(
                                       fC->SymName(simple_port_name)));
                      errors->addError(
                          ErrorDefinition::COMP_MODPORT_UNDEFINED_PORT, loc);
                    }
                  }
                  Signal signal(
                      m_module, fC, port_declaration, simple_port_name,
                      VObjectType::paData_type_or_implicit, port_direction_type,
                      InvalidNodeId, InvalidNodeId, false);
                  m_module->insertModport(modportsymb, signal, modportname);
                  modport_simple_port = fC->Sibling(modport_simple_port);
                }
              } else if (port_declaration_type ==
                         VObjectType::
                             paModport_hierarchical_ports_declaration) {
              } else if (port_declaration_type ==
                         VObjectType::paModport_tf_ports_declaration) {
              } else {
                // CLOCKING
                NodeId clocking_block_name = port_declaration;
                SymbolId clocking_block_symbol =
                    symbols->registerSymbol(fC->SymName(clocking_block_name));
                ClockingBlock* cb =
                    m_module->getClockingBlock(clocking_block_symbol);
                if (cb == nullptr) {
                  Location loc(fC->getFileId(clocking_block_name),
                               fC->Line(clocking_block_name),
                               fC->Column(clocking_block_name),
                               clocking_block_symbol);
                  errors->addError(
                      ErrorDefinition::COMP_MODPORT_UNDEFINED_CLOCKING_BLOCK,
                      loc);
                } else {
                  m_module->insertModport(modportsymb, *cb);
                }
              }
              modport_ports_declaration =
                  fC->Sibling(modport_ports_declaration);
            }
          }
          break;
        case VObjectType::paInitial_construct: {
          if (collectType != CollectType::OTHER) break;
          uhdm::Initial* init = m_helper.compileInitialBlock(
              m_module, fC, id, m_compileDesign, m_module->getUhdmModel());
          uhdm::ProcessCollection* processes = m_module->getProcesses();
          if (processes == nullptr) {
            m_module->setProcesses(m_compileDesign->getSerializer()
                                       .makeCollection<uhdm::Process>());
            processes = m_module->getProcesses();
          }
          processes->emplace_back(init);
          break;
        }
        case VObjectType::paFinal_construct: {
          if (collectType != CollectType::OTHER) break;
          uhdm::FinalStmt* final = m_helper.compileFinalBlock(
              m_module, fC, id, m_compileDesign, m_module->getUhdmModel());
          uhdm::ProcessCollection* processes = m_module->getProcesses();
          if (processes == nullptr) {
            m_module->setProcesses(m_compileDesign->getSerializer()
                                       .makeCollection<uhdm::Process>());
            processes = m_module->getProcesses();
          }
          processes->emplace_back(final);
          break;
        }
        case VObjectType::paParameter_declaration: {
          if (collectType != CollectType::DEFINITION) break;

          NodeId list_of_type_assignments = fC->Child(id);
          if (fC->Type(list_of_type_assignments) ==
                  VObjectType::paType_assignment_list ||
              fC->Type(list_of_type_assignments) == VObjectType::TYPE) {
            // Type param
            m_helper.compileParameterDeclaration(
                m_module, fC, list_of_type_assignments, m_compileDesign,
                m_instance != nullptr ? Reduce::Yes : Reduce::No, false,
                m_instance, ParameterPortListId, false);

          } else {
            m_helper.compileParameterDeclaration(
                m_module, fC, id, m_compileDesign,
                m_instance != nullptr ? Reduce::Yes : Reduce::No, false,
                m_instance, ParameterPortListId, false);
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
            m_helper.compileParameterDeclaration(
                m_module, fC, list_of_type_assignments, m_compileDesign,
                m_instance != nullptr ? Reduce::Yes : Reduce::No, true,
                m_instance, ParameterPortListId, false);

          } else {
            m_helper.compileParameterDeclaration(
                m_module, fC, id, m_compileDesign,
                m_instance != nullptr ? Reduce::Yes : Reduce::No, true,
                m_instance, ParameterPortListId, false);
          }
          break;
        }
        case VObjectType::paBind_directive: {
          if (collectType != CollectType::OTHER) break;
          m_helper.compileBindStmt(m_module, fC, id, m_compileDesign,
                                   m_instance);
          break;
        }
        case VObjectType::paParam_assignment:
        case VObjectType::paDefparam_assignment: {
          if (collectType != CollectType::OTHER) break;
          FileCNodeId fnid(fC, id);
          m_module->addObject(type, fnid);
          break;
        }
        case VObjectType::paLet_declaration: {
          if (collectType != CollectType::FUNCTION) break;
          m_helper.compileLetDeclaration(m_module, fC, id, m_compileDesign);
          break;
        }
        case VObjectType::ENDINTERFACE: {
          if (collectType != CollectType::DEFINITION) break;
          NodeId InterfaceIdentifier = fC->Sibling(id);
          if (InterfaceIdentifier) {
            NodeId label = fC->Child(InterfaceIdentifier);
            const std::string_view endLabel = fC->SymName(label);
            m_module->setEndLabel(endLabel);
            std::string_view moduleName = m_module->getName();
            moduleName = StringUtils::ltrim_until(moduleName, '@');
            moduleName = StringUtils::ltrim_until(moduleName, ':');
            moduleName = StringUtils::ltrim_until(moduleName, ':');
            if (endLabel != moduleName) {
              Location loc(fC->getFileId(m_module->getNodeIds()[0]),
                           fC->Line(m_module->getNodeIds()[0]),
                           fC->Column(m_module->getNodeIds()[0]),
                           symbols->registerSymbol(moduleName));
              Location loc2(fC->getFileId(id), fC->Line(id), fC->Column(id),
                            symbols->registerSymbol(endLabel));
              errors->addError(ErrorDefinition::COMP_UNMATCHED_LABEL, loc,
                               loc2);
            }
          }
          break;
        }
        case VObjectType::paGenvar_declaration: {
          if (collectType != CollectType::OTHER) break;
          FileCNodeId fnid(fC, id);
          m_module->addObject(type, fnid);
          if (m_instance) break;
          if (uhdm::AnyCollection* vars =
                  m_helper.compileGenVars(m_module, fC, id, m_compileDesign)) {
            if (m_module->getGenVars() == nullptr) {
              m_module->setGenVars(
                  m_compileDesign->getSerializer().makeCollection<uhdm::Any>());
            }
            for (auto v : *vars) {
              m_module->getGenStmts()->emplace_back(v);
            }
          }
          break;
        }
        case VObjectType::paGenerate_region:
        case VObjectType::paLoop_generate_construct:
        case VObjectType::paGenerate_interface_loop_statement:
        case VObjectType::paGenerate_interface_conditional_statement:
        case VObjectType::paConditional_generate_construct: {
          if (collectType != CollectType::OTHER) break;
          FileCNodeId fnid(fC, id);
          m_module->addObject(type, fnid);
          if (m_instance) break;
          if (uhdm::AnyCollection* stmts =
                  m_helper.compileGenStmt(m_module, fC, id, m_compileDesign)) {
            if (m_module->getGenStmts() == nullptr) {
              m_module->setGenStmts(
                  m_compileDesign->getSerializer().makeCollection<uhdm::Any>());
            }
            for (auto st : *stmts) {
              m_module->getGenStmts()->emplace_back(st);
            }
          }
          break;
        }
        default:
          break;
      }

      if (current.m_sibling) stack.push(current.m_sibling);
      if (current.m_child && (!skipChildren)) {
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
  }

  if (collectType == CollectType::DEFINITION) {
    for (Signal* port : m_module->getPorts()) {
      bool found = false;
      for (Signal* sig : m_module->getSignals()) {
        if (sig->getName() == port->getName()) {
          found = true;
          break;
        }
      }
      if (found == false) {
        m_module->addSignal(port);
      }
    }
  }
  return true;
}

bool CompileModule::checkModule_() {
  SymbolTable* const symbols = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  ErrorContainer* const errors = m_session->getErrorContainer();
  int32_t countMissingType = 0;
  int32_t countMissingDirection = 0;
  Location* missingTypeLoc = nullptr;
  Location* missingDirectionLoc = nullptr;
  for (Signal* port : m_module->m_ports) {
    if (port->isInterface()) continue;
    const FileContent* fC = port->getFileContent();
    NodeId portId = port->getNodeId();
    if (fC->Type(portId) == VObjectType::paPort) {
      NodeId expName = fC->Child(portId);
      if (fC->Type(expName) == VObjectType::STRING_CONST) {
        // Port expression
        continue;
      }
    }
    if (port->getType() == VObjectType::paData_type_or_implicit) {
      if (port->getDirection() == VObjectType::paPortDir_Out ||
          port->getDirection() == VObjectType::paPortDir_Inout) {
        if (countMissingType == 0)
          missingTypeLoc = new Location(
              fileSystem->copy(
                  port->getFileContent()->getFileId(port->getNameId()),
                  symbols),
              port->getFileContent()->Line(port->getNameId()),
              port->getFileContent()->Column(port->getNameId()),
              symbols->registerSymbol(port->getName()));
        countMissingType++;
      }
    }
    if (port->getDirection() == VObjectType::NO_TYPE) {
      if (countMissingDirection == 0)
        missingDirectionLoc = new Location(
            fileSystem->copy(
                port->getFileContent()->getFileId(port->getNameId()), symbols),
            port->getFileContent()->Line(port->getNameId()),
            port->getFileContent()->Column(port->getNameId()),
            symbols->registerSymbol(port->getName()));
      countMissingDirection++;
    }
  }
  if (countMissingType) {
    Location countLoc(
        symbols->registerSymbol(std::to_string(countMissingType - 1)));
    if (countMissingType - 1 > 0) {
      errors->addError(ErrorDefinition::COMP_PORT_MISSING_TYPE, *missingTypeLoc,
                       countLoc);
    } else {
      errors->addError(ErrorDefinition::COMP_PORT_MISSING_TYPE,
                       *missingTypeLoc);
    }
    delete missingTypeLoc;
  }
  if (countMissingDirection) {
    Location countLoc(
        symbols->registerSymbol(std::to_string(countMissingDirection - 1)));
    if (countMissingDirection - 1 > 0) {
      errors->addError(ErrorDefinition::COMP_PORT_MISSING_DIRECTION,
                       *missingDirectionLoc, countLoc);
    } else {
      errors->addError(ErrorDefinition::COMP_PORT_MISSING_DIRECTION,
                       *missingDirectionLoc);
    }
    if (countMissingType) {
      errors->addError(ErrorDefinition::COMP_UNSPECIFIED_PORT,
                       *missingDirectionLoc);
    }
    delete missingDirectionLoc;
  }

  return true;
}

bool CompileModule::checkInterface_() {
  SymbolTable* const symbols = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  ErrorContainer* const errors = m_session->getErrorContainer();
  int32_t countMissingType = 0;
  Location* missingTypeLoc = nullptr;
  for (auto& port : m_module->m_ports) {
    if (port->getType() == VObjectType::paData_type_or_implicit) {
      if (port->getDirection() == VObjectType::paPortDir_Out ||
          port->getDirection() == VObjectType::paPortDir_Inout) {
        if (countMissingType == 0)
          missingTypeLoc = new Location(
              fileSystem->copy(
                  port->getFileContent()->getFileId(port->getNodeId()),
                  symbols),
              port->getFileContent()->Line(port->getNodeId()), 0,
              symbols->registerSymbol(port->getName()));
        countMissingType++;
      }
    }
  }
  if (countMissingType) {
    Location countLoc(
        symbols->registerSymbol(std::to_string(countMissingType - 1)));
    if (countMissingType - 1 > 0) {
      errors->addError(ErrorDefinition::COMP_PORT_MISSING_TYPE, *missingTypeLoc,
                       countLoc);
    } else {
      errors->addError(ErrorDefinition::COMP_PORT_MISSING_TYPE,
                       *missingTypeLoc);
    }
    delete missingTypeLoc;
  }
  return true;
}

void CompileModule::compileClockingBlock_(const FileContent* fC, NodeId id) {
  /*
    n<cb> u<12> t<StringConst> p<21> s<20> l<39>
    n<> u<13> t<Edge_Posedge> p<19> s<18> l<39>
    n<clk> u<14> t<StringConst> p<17> s<16> l<39>
    n<> u<15> t<Bit_select> p<16> l<39>
    n<> u<16> t<Select> p<17> c<15> l<39>
    n<> u<17> t<Primary> p<18> c<14> l<39>
    n<> u<18> t<Expression> p<19> c<17> l<39>
    n<> u<19> t<Event_expression> p<20> c<13> l<39>
    n<> u<20> t<Clocking_event> p<21> c<19> l<39>
    n<> u<21> t<Clocking_declaration> p<22> c<12> l<39>
   */

  SymbolTable* const symbols = m_session->getSymbolTable();
  NodeId clocking_block_type = fC->Child(id);
  NodeId clocking_block_name;
  SymbolId clocking_block_symbol;
  ClockingBlock::Type type = ClockingBlock::Regular;
  if (fC->Type(clocking_block_type) == VObjectType::DEFAULT)
    type = ClockingBlock::Default;
  else if (fC->Type(clocking_block_type) == VObjectType::GLOBAL)
    type = ClockingBlock::Global;
  else if (fC->Type(clocking_block_type) == VObjectType::STRING_CONST)
    clocking_block_name = clocking_block_type;
  NodeId clocking_event = fC->Sibling(clocking_block_type);
  if (fC->Type(clocking_event) == VObjectType::STRING_CONST) {
    clocking_block_name = clocking_event;
    clocking_event = fC->Sibling(clocking_block_name);
  }
  if (clocking_block_name)
    clocking_block_symbol =
        symbols->registerSymbol(fC->SymName(clocking_block_name));
  else
    clocking_block_symbol = symbols->registerSymbol("unnamed_clocking_block");
  uhdm::ClockingBlock* cblock = m_helper.compileClockingBlock(
      m_module, fC, id, m_compileDesign, nullptr, m_instance);
  ClockingBlock cb(fC, clocking_block_type, clocking_event, type, cblock);
  m_module->addClockingBlock(clocking_block_symbol, cb);
}

}  // namespace SURELOG
