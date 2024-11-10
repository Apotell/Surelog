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
 * File:   CompileStep.h
 * Author: alain
 *
 * Created on July 5, 2017, 10:44 PM
 */

#ifndef SURELOG_COMPILESTEP_H
#define SURELOG_COMPILESTEP_H
#pragma once

#include <Surelog/Common/NodeId.h>
#include <Surelog/Common/SymbolId.h>
#include <Surelog/SourceCompile/VObjectTypes.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace SURELOG {

class VObject;

class CompileStep {
 public:
  CompileStep() = default;
  CompileStep(const CompileStep& orig) = default;
  virtual ~CompileStep() = default;

  virtual VObject Object(NodeId index) const = 0;

  virtual NodeId UniqueId(NodeId index) const = 0;

  virtual SymbolId Name(NodeId index) const = 0;

  virtual NodeId Child(NodeId index) const = 0;

  virtual NodeId Sibling(NodeId index) const = 0;

  virtual NodeId Definition(NodeId index) const = 0;

  virtual NodeId Parent(NodeId index) const = 0;

  virtual VObjectType Type(NodeId index) const = 0;

  virtual uint32_t Line(NodeId index) const = 0;

  virtual std::string_view Symbol(SymbolId id) const = 0;

  virtual NodeId sl_get(NodeId parent,
                        VObjectType type) const = 0;  // Get first item of type

  virtual NodeId sl_parent(
      NodeId parent,
      VObjectType type) const = 0;  // Get first parent item of type

  virtual NodeId sl_parent(NodeId parent, const VObjectTypeUnorderedSet& types,
                           VObjectType& actualType) const = 0;

  virtual std::vector<NodeId> sl_get_all(
      NodeId parent, VObjectType type) const = 0;  // get all items of type

  virtual NodeId sl_collect(
      NodeId parent,
      VObjectType type) const = 0;  // Recursively search for first item of type

  virtual std::vector<NodeId> sl_collect_all(
      NodeId parent,
      VObjectType type) const = 0;  // Recursively search for all items of type

  virtual std::string_view SymName(NodeId index) const = 0;

 private:
};

};  // namespace SURELOG

#endif /* SURELOG_COMPILESTEP_H */
