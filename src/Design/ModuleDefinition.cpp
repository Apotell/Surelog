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

#include "Surelog/Design/ModuleDefinition.h"

#include <uhdm/Serializer.h>
#include <uhdm/interface.h>
#include <uhdm/interface_typespec.h>
#include <uhdm/modport.h>
#include <uhdm/module.h>
#include <uhdm/module_typespec.h>
#include <uhdm/udp_defn.h>
#include <uhdm/udp_defn_typespec.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "Surelog/Common/Session.h"
#include "Surelog/Design/ClockingBlock.h"
#include "Surelog/Design/DesignComponent.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/Design/Modport.h"
#include "Surelog/Design/Signal.h"
#include "Surelog/SourceCompile/VObjectTypes.h"
#include "Surelog/Utils/StringUtils.h"

namespace SURELOG {

VObjectType ModuleDefinition::getType() const {
  return (m_fileContents.empty()) ? VObjectType::paN_input_gate_instance
                                  : m_fileContents[0]->Type(m_nodeIds[0]);
}

ModuleDefinition::ModuleDefinition(Session* session, std::string_view name,
                                   const FileContent* fC, NodeId nodeId,
                                   uhdm::Serializer& serializer)
    : DesignComponent(session, fC, nullptr), m_name(name), m_udpDefn(nullptr) {
  addFileContent(fC, nodeId);
  switch (fC->Type(nodeId)) {
    // case VObjectType::paConfig_declaration:
    case VObjectType::paUdp_declaration: {
      uhdm::UdpDefn* const instance = serializer.make<uhdm::UdpDefn>();
      if (!name.empty()) instance->setDefName(name);
      fC->populateCoreMembers(fC->sl_collect(nodeId, VObjectType::PRIMITIVE),
                              nodeId, instance);
      setUhdmModel(instance);

      uhdm::UdpDefnTypespec* const tps =
          serializer.make<uhdm::UdpDefnTypespec>();
      tps->setName(
          fC->SymName(fC->sl_collect(nodeId, VObjectType::STRING_CONST)));
      tps->setUdpDefn(instance);
      setUhdmTypespecModel(tps);
    } break;

    case VObjectType::paInterface_declaration: {
      uhdm::Interface* const instance = serializer.make<uhdm::Interface>();
      if (!name.empty()) instance->setName(name);
      fC->populateCoreMembers(fC->sl_collect(nodeId, VObjectType::INTERFACE),
                              nodeId, instance);
      setUhdmModel(instance);

      uhdm::InterfaceTypespec* const tps =
          serializer.make<uhdm::InterfaceTypespec>();
      tps->setName(fC->SymName(
          fC->sl_collect(nodeId, VObjectType::paInterface_identifier)));
      tps->setInterface(instance);
      setUhdmTypespecModel(tps);
    } break;

    default: {
      uhdm::Module* const instance = serializer.make<uhdm::Module>();
      if (!name.empty()) instance->setName(name);
      fC->populateCoreMembers(
          fC->sl_collect(nodeId, VObjectType::paModule_keyword), nodeId,
          instance);
      setUhdmModel(instance);

      uhdm::ModuleTypespec* const tps = serializer.make<uhdm::ModuleTypespec>();
      tps->setName(
          fC->SymName(fC->sl_collect(nodeId, VObjectType::STRING_CONST)));
      tps->setModule(instance);
      setUhdmTypespecModel(tps);
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

void ModuleDefinition::insertModport(std::string_view modport,
                                     const Signal& signal, NodeId nodeId) {
  ModportSignalMap::iterator itr = m_modportSignalMap.find(modport);
  if (itr == m_modportSignalMap.end()) {
    const FileContent* const fC = m_fileContents[0];
    auto it = m_modportSignalMap.emplace(
        std::piecewise_construct, std::forward_as_tuple(modport),
        std::forward_as_tuple(this, modport, fC, nodeId));
    it.first->second.addSignal(signal);

    uhdm::Serializer* const serializer = getUhdmModel()->getSerializer();
    uhdm::Interface* const instance = getUhdmModel<uhdm::Interface>();

    uhdm::Modport* const mp = serializer->make<uhdm::Modport>();
    mp->setName(modport);
    mp->setParent(getUhdmModel());
    mp->setInterface(instance);
    fC->populateCoreMembers(nodeId, nodeId, mp);
    it.first->second.setUhdmModel(mp);
    it.first->second.setInterface(instance);
  } else {
    itr->second.addSignal(signal);
  }
}

const Signal* ModuleDefinition::getModportSignal(std::string_view modport,
                                                 NodeId port) const {
  ModportSignalMap::const_iterator itr = m_modportSignalMap.find(modport);
  if (itr == m_modportSignalMap.end()) {
    return nullptr;
  } else {
    for (auto& sig : itr->second.getPorts()) {
      if (sig.getNodeId() == port) {
        return &sig;
      }
    }
  }
  return nullptr;
}

Modport* ModuleDefinition::getModport(std::string_view modport) {
  ModportSignalMap::iterator itr = m_modportSignalMap.find(modport);
  if (itr == m_modportSignalMap.end()) {
    return nullptr;
  } else {
    return &itr->second;
  }
}

void ModuleDefinition::insertModport(std::string_view modport,
                                     const ClockingBlock& cb) {
  ModportClockingBlockMap::iterator itr =
      m_modportClockingBlockMap.find(modport);
  if (itr == m_modportClockingBlockMap.end()) {
    auto it = m_modportClockingBlockMap.emplace(std::piecewise_construct,
                                                std::forward_as_tuple(modport),
                                                std::forward_as_tuple());
    it.first->second.emplace_back(cb);
  } else {
    itr->second.emplace_back(cb);
  }
}

const ClockingBlock* ModuleDefinition::getModportClockingBlock(
    std::string_view modport, NodeId port) const {
  auto itr = m_modportClockingBlockMap.find(modport);
  if (itr == m_modportClockingBlockMap.end()) {
    return nullptr;
  } else {
    for (auto& cb : itr->second) {
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
    return itr->second;
  }
}

}  // namespace SURELOG
