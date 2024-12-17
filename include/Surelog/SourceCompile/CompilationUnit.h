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
 * File:   CompilationUnit.h
 * Author: alain
 *
 * Created on April 5, 2017, 9:16 PM
 */

#ifndef SURELOG_COMPILATIONUNIT_H
#define SURELOG_COMPILATIONUNIT_H
#pragma once

#include <Surelog/Common/Containers.h>
#include <Surelog/Common/NodeId.h>
#include <Surelog/Design/TimeInfo.h>

namespace SURELOG {

class MacroInfo;

class CompilationUnit {
 public:
  explicit CompilationUnit(bool fileUnit);
  CompilationUnit(const CompilationUnit& orig) = delete;
  virtual ~CompilationUnit() = default;

  void setInDesignElement() { m_inDesignElement = true; }
  void unsetInDesignElement() { m_inDesignElement = false; }
  bool isInDesignElement() const { return m_inDesignElement; }
  bool isFileUnit() const { return m_fileUnit; }

  void registerMacroInfo(MacroInfo* macro);
  MacroInfo* getMacroInfo(std::string_view macroName);

  const MacroStorage& getMacros() const { return m_macros; }

  /* Following methods deal with `timescale */
  void setCurrentTimeInfo(PathId fileId);
  std::vector<TimeInfo>& getTimeInfo() { return m_timeInfo; }
  void recordTimeInfo(TimeInfo& info);
  TimeInfo& getTimeInfo(PathId fileId, uint32_t line);

  /* Following methods deal with `default_nettype */
  std::vector<NetTypeInfo>& getDefaultNetType() { return m_defaultNetTypes; }
  void recordDefaultNetType(NetTypeInfo& info);
  VObjectType getDefaultNetType(PathId fileId, uint32_t line);

  NodeId generateUniqueDesignElemId() {
    m_uniqueIdGenerator++;
    return m_uniqueIdGenerator;
  }
  NodeId generateUniqueNodeId() {
    m_uniqueNodeIdGenerator++;
    return m_uniqueNodeIdGenerator;
  }

 private:
  const bool m_fileUnit;
  bool m_inDesignElement;

  MacroStorage m_macros;

  std::vector<TimeInfo> m_timeInfo;
  std::vector<NetTypeInfo> m_defaultNetTypes;
  TimeInfo m_noTimeInfo;

  /* Design Info helper data */
  NodeId m_uniqueIdGenerator;
  NodeId m_uniqueNodeIdGenerator;
};

};  // namespace SURELOG

#endif /* SURELOG_COMPILATIONUNIT_H */
