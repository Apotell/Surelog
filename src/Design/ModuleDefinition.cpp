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
 * File:   ModuleDefinition.cpp
 * Author: alain
 *
 * Created on October 20, 2017, 10:29 PM
 */

#include <Surelog/Design/FileContent.h>
#include <Surelog/Design/ModPort.h>
#include <Surelog/Design/ModuleDefinition.h>
#include <uhdm/Serializer.h>
#include <uhdm/interface_inst.h>
#include <uhdm/module_inst.h>
#include <uhdm/udp_defn.h>

namespace SURELOG {

VObjectType ModuleDefinition::getType() const {
  return (m_fileContents.empty()) ? VObjectType::paN_input_gate_instance
                                  : m_fileContents[0]->Type(m_nodeIds[0]);
}

ModuleDefinition::ModuleDefinition(Session* session, std::string_view name,
                                   const FileContent* fC, NodeId nodeId,
                                   UHDM::Serializer& serializer)
    : DesignComponent(session, fC, nullptr), m_name(name), m_udpDefn(nullptr) {
  addFileContent(fC, nodeId);
  m_unelabModule = this;
  switch (fC->Type(nodeId)) {
    // case VObjectType::paConfig_declaration:
    case VObjectType::paUdp_declaration: {
      UHDM::udp_defn* const instance = serializer.MakeUdp_defn();
      if (!name.empty()) instance->VpiDefName(name);
      fC->populateCoreMembers(fC->sl_collect(nodeId, VObjectType::paPRIMITIVE),
                              nodeId, instance);
      setUhdmModel(instance);
    } break;

    case VObjectType::paInterface_declaration: {
      UHDM::interface_inst* const instance = serializer.MakeInterface_inst();
      if (!name.empty()) instance->VpiName(name);
      fC->populateCoreMembers(fC->sl_collect(nodeId, VObjectType::paINTERFACE),
                              nodeId, instance);
      setUhdmModel(instance);
    } break;

    default: {
      UHDM::module_inst* const instance = serializer.MakeModule_inst();
      if (!name.empty()) instance->VpiName(name);
      fC->populateCoreMembers(
          fC->sl_collect(nodeId, VObjectType::paModule_keyword), nodeId,
          instance);
      setUhdmModel(instance);
    } break;
  }
}

bool ModuleDefinition::isInstance() const {
  const VObjectType type = getType();
  return ((type == VObjectType::paN_input_gate_instance) ||
          (type == VObjectType::paModule_declaration) ||
          (type == VObjectType::paUdp_declaration));
}

uint32_t ModuleDefinition::getSize() const {
  uint32_t size = 0;
  for (size_t i = 0; i < m_fileContents.size(); i++) {
    NodeId end = m_nodeIds[i];
    NodeId begin = m_fileContents[i]->Child(end);
    size += (RawNodeId)(end - begin);
  }
  return size;
}

void ModuleDefinition::insertModPort(std::string_view modport,
                                     const Signal& signal, NodeId nodeId) {
  ModPortSignalMap::iterator itr = m_modportSignalMap.find(modport);
  if (itr == m_modportSignalMap.end()) {
    ModPort modp(this, modport, m_fileContents[0], nodeId);
    modp.addSignal(signal);
    m_modportSignalMap.emplace(modport, modp);
  } else {
    (*itr).second.addSignal(signal);
  }
}

const Signal* ModuleDefinition::getModPortSignal(std::string_view modport,
                                                 NodeId port) const {
  ModPortSignalMap::const_iterator itr = m_modportSignalMap.find(modport);
  if (itr == m_modportSignalMap.end()) {
    return nullptr;
  } else {
    for (auto& sig : (*itr).second.getPorts()) {
      if (sig.getNodeId() == port) {
        return &sig;
      }
    }
  }
  return nullptr;
}

ModPort* ModuleDefinition::getModPort(std::string_view modport) {
  ModPortSignalMap::iterator itr = m_modportSignalMap.find(modport);
  if (itr == m_modportSignalMap.end()) {
    return nullptr;
  } else {
    return &(*itr).second;
  }
}

void ModuleDefinition::insertModPort(std::string_view modport,
                                     ClockingBlock& cb) {
  ModPortClockingBlockMap::iterator itr =
      m_modportClockingBlockMap.find(modport);
  if (itr == m_modportClockingBlockMap.end()) {
    std::vector<ClockingBlock> cbs;
    cbs.push_back(cb);
    m_modportClockingBlockMap.emplace(modport, cbs);
  } else {
    (*itr).second.push_back(cb);
  }
}

const ClockingBlock* ModuleDefinition::getModPortClockingBlock(
    std::string_view modport, NodeId port) const {
  auto itr = m_modportClockingBlockMap.find(modport);
  if (itr == m_modportClockingBlockMap.end()) {
    return nullptr;
  } else {
    for (auto& cb : (*itr).second) {
      if (cb.getNodeId() == port) {
        return &cb;
      }
    }
  }
  return nullptr;
}

ClassDefinition* ModuleDefinition::getClassDefinition(std::string_view name) {
  auto itr = m_classDefinitions.find(name);
  if (itr == m_classDefinitions.end()) {
    return nullptr;
  } else {
    return (*itr).second;
  }
}

}  // namespace SURELOG
