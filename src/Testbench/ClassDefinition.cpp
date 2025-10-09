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
 * File:   ClassDefinition.cpp
 * Author: alain
 *
 * Created on June 1, 2018, 10:12 PM
 */
#include "Surelog/Testbench/ClassDefinition.h"

#include <uhdm/Serializer.h>
#include <uhdm/class_defn.h>
#include <uhdm/uhdm.h>

#include <cstdint>
#include <string_view>

#include "Surelog/Common/NodeId.h"
#include "Surelog/Design/DataType.h"
#include "Surelog/Design/DesignComponent.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/Design/Parameter.h"
#include "Surelog/SourceCompile/VObjectTypes.h"
#include "Surelog/Testbench/Constraint.h"
#include "Surelog/Testbench/CoverGroupDefinition.h"
#include "Surelog/Testbench/Property.h"

namespace SURELOG {

ClassDefinition::ClassDefinition(Session* session, std::string_view name,
                                 Library* library, DesignComponent* container,
                                 const FileContent* fC, NodeId nodeId,
                                 ClassDefinition* parent,
                                 uhdm::Serializer& serializer)
    : DesignComponent(session, container ? container : fC, nullptr),
      DataType(fC, nodeId, name,
               fC ? fC->Type(nodeId) : VObjectType::paClass_declaration),
      m_name(name),
      m_library(library),
      m_container(container),
      m_parent(parent) {
  m_category = DataType::Category::CLASS;
  addFileContent(fC, nodeId);

  uhdm::ClassDefn* const instance = serializer.make<uhdm::ClassDefn>();
  NodeId stId = fC->sl_collect(nodeId, VObjectType::STRING_CONST);
  if (!name.empty()) instance->setName(name);
  if (nodeId && (fC != nullptr)) {
    fC->populateCoreMembers(fC->sl_collect(nodeId, VObjectType::CLASS), nodeId,
                            instance);
  }
  if (stId) fC->populateCoreMembers(stId, stId, instance->getNameObj());
  if (container != nullptr) instance->setParent(container->getUhdmModel());
  setUhdmModel(instance);
}

void ClassDefinition::setContainer(DesignComponent* container) {
  getUhdmModel()->setParent(nullptr, true);
  m_container = container;

  if (m_container != nullptr)
    getUhdmModel()->setParent(m_container->getUhdmModel());
}

uint32_t ClassDefinition::getSize() const {
  NodeId end = m_nodeIds[0];
  NodeId begin = m_fileContents[0]->Child(end);
  uint32_t size = (RawNodeId)(end - begin);
  return size;
}

Property* ClassDefinition::getProperty(std::string_view name) const {
  PropertyMap::const_iterator itr = m_properties.find(name);
  if (itr == m_properties.end()) {
    for (const auto& parent : getBaseClassMap()) {
      if (parent.second) {
        const ClassDefinition* cparent =
            datatype_cast<const ClassDefinition*>(parent.second);
        if (cparent) {
          Property* d = cparent->getProperty(name);
          if (d) return d;
        }
      }
    }
    return nullptr;
  } else {
    return itr->second;
  }
}

const DataType* ClassDefinition::getDataType(Design* design,
                                             std::string_view name) const {
  if (const DataType* const dt = DesignComponent::getDataType(design, name)) {
    return dt;
  } else if (const DataType* const dt = getClass(name)) {
    return dt;
  }
  return nullptr;
}

void ClassDefinition::insertProperty(Property* p) {
  m_properties.emplace(p->getName(), p);
}

Function* ClassDefinition::getFunction(std::string_view name) const {
  FunctionMap::const_iterator itr = m_functions.find(name);
  if (itr != m_functions.end()) {
    return itr->second;
  }

  for (const auto& parent : getBaseClassMap()) {
    if (parent.second) {
      const ClassDefinition* cparent =
          datatype_cast<const ClassDefinition*>(parent.second);
      if (cparent) {
        Function* d = cparent->getFunction(name);
        if (d) return d;
      }
    }
  }

  if (m_container) {
    return m_container->getFunction(name);
  }

  return nullptr;
}

TaskMethod* ClassDefinition::getTask(std::string_view name) const {
  TaskMap::const_iterator itr = m_tasks.find(name);
  if (itr == m_tasks.end()) {
    for (const auto& parent : getBaseClassMap()) {
      if (parent.second) {
        const ClassDefinition* cparent =
            datatype_cast<const ClassDefinition*>(parent.second);
        if (cparent) {
          TaskMethod* d = cparent->getTask(name);
          if (d) return d;
        }
      }
    }
    return nullptr;
  } else {
    return itr->second;
  }
}

void ClassDefinition::insertTask(TaskMethod* p) {
  m_tasks.emplace(p->getName(), p);
}

Constraint* ClassDefinition::getConstraint(std::string_view name) {
  ConstraintMap::iterator itr = m_constraints.find(name);
  if (itr == m_constraints.end()) {
    return nullptr;
  } else {
    return itr->second;
  }
}

void ClassDefinition::insertConstraint(Constraint* p) {
  m_constraints.emplace(p->getName(), p);
}

ClassDefinition* ClassDefinition::getClass(std::string_view name) {
  ClassMap::iterator itr = m_classes.find(name);
  if (itr == m_classes.end()) {
    return nullptr;
  } else {
    return itr->second;
  }
}

const ClassDefinition* ClassDefinition::getClass(std::string_view name) const {
  ClassMap::const_iterator itr = m_classes.find(name);
  if (itr == m_classes.end()) {
    return nullptr;
  } else {
    return itr->second;
  }
}

void ClassDefinition::insertClass(ClassDefinition* p) {
  m_classes.emplace(p->getName(), p);
}

CoverGroupDefinition* ClassDefinition::getCoverGroup(std::string_view name) {
  CoverGroupMap::iterator itr = m_coverGroups.find(name);
  if (itr == m_coverGroups.end()) {
    return nullptr;
  } else {
    return itr->second;
  }
}

void ClassDefinition::insertCoverGroup(CoverGroupDefinition* p) {
  m_coverGroups.emplace(p->getName(), p);
}

const DataType* ClassDefinition::getBaseClass(std::string_view name) const {
  BaseClassMap::const_iterator itr = m_baseClasses.find(name);
  if (itr == m_baseClasses.end()) {
    return nullptr;
  } else {
    return itr->second;
  }
}

void ClassDefinition::insertBaseClass(DataType* p) {
  m_baseClasses.emplace(p->getName(), p);
}

const DataType* ClassDefinition::getBaseDataType(std::string_view name) const {
  const DataTypeMap& dataTypes = getDataTypeMap();
  DataTypeMap::const_iterator itr = dataTypes.find(name);
  if (itr == dataTypes.end()) {
    for (const auto& parent : getBaseClassMap()) {
      if (parent.second) {
        const ClassDefinition* cparent =
            datatype_cast<const ClassDefinition*>(parent.second);
        if (cparent) {
          const DataType* d = cparent->getBaseDataType(name);
          if (d) return d;
        }
      }
    }
    return nullptr;
  } else {
    return itr->second;
  }
}

bool ClassDefinition::hasCompleteBaseSpecification() const {
  for (const auto& parent : getBaseClassMap()) {
    if (parent.second) {
      const ClassDefinition* cparent =
          datatype_cast<const ClassDefinition*>(parent.second);
      if (cparent) {
        return cparent->hasCompleteBaseSpecification();
      }
      const Parameter* param = datatype_cast<Parameter>(parent.second);
      if (param) return false;
    }
  }
  return true;
}

}  // namespace SURELOG
