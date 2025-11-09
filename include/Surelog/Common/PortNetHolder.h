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
 * File:   PortNetHolder.h
 * Author: alain
 *
 * Created on January 30, 2020, 8:31 PM
 */

#ifndef SURELOG_PORTNETHOLDER_H
#define SURELOG_PORTNETHOLDER_H
#pragma once

// UHDM
#include <uhdm/containers.h>
#include <uhdm/uhdm_forward_decl.h>

#include <algorithm>
#include <vector>

namespace SURELOG {

class Signal;

class PortNetHolder {
 public:
  virtual ~PortNetHolder() = default;  // virtual as used as interface

  const std::vector<Signal*>& getPorts() const { return m_ports; }
  const std::vector<Signal*>& getSignals() const { return m_signals; }

  void addPort(Signal* signal) { m_ports.emplace_back(signal); }

  void addSignal(Signal* signal) { m_signals.emplace_back(signal); }

  bool removePort(Signal* signal) {
    auto it = std::find(m_ports.begin(), m_ports.end(), signal);
    if (it != m_ports.end()) {
      m_ports.erase(it);
      return true;
    }
    return false;
  }

  bool removeSignal(Signal* signal) {
    auto it = std::find(m_signals.begin(), m_signals.end(), signal);
    if (it != m_signals.end()) {
      m_signals.erase(it);
      return true;
    }
    return false;
  }

  uhdm::ContAssignCollection* getContAssigns() const { return m_contAssigns; }

  void setContAssigns(uhdm::ContAssignCollection* contAssigns) {
    m_contAssigns = contAssigns;
  }

  uhdm::ProcessCollection* getProcesses() const { return m_processes; }
  void setProcesses(uhdm::ProcessCollection* processes) {
    m_processes = processes;
  }

  uhdm::AnyCollection* getParameters() const { return m_parameters; }
  void setParameters(uhdm::AnyCollection* parameters) {
    m_parameters = parameters;
  }

  uhdm::AnyCollection* getAssertions() const { return m_assertions; }
  void setAssertions(uhdm::AnyCollection* assertions) {
    m_assertions = assertions;
  }

  uhdm::ParamAssignCollection* getParamAssigns() const {
    return m_paramAssigns;
  }
  void setParamAssigns(uhdm::ParamAssignCollection* paramAssigns) {
    m_paramAssigns = paramAssigns;
  }

  uhdm::ParamAssignCollection* getOrigParamAssigns() const {
    return m_origParamAssigns;
  }
  void setOrigParamAssigns(uhdm::ParamAssignCollection* paramAssigns) {
    m_origParamAssigns = paramAssigns;
  }

  uhdm::TaskFuncCollection* getTaskFuncs() const { return m_taskFuncs; }

  void setTaskFuncs(uhdm::TaskFuncCollection* taskFuncs) {
    m_taskFuncs = taskFuncs;
  }

  uhdm::TaskFuncDeclCollection* getTaskFuncDecls() const {
    return m_taskFuncDecls;
  }

  void setTaskFuncDecls(uhdm::TaskFuncDeclCollection* taskFuncDecls) {
    m_taskFuncDecls = taskFuncDecls;
  }

 protected:
  std::vector<Signal*> m_ports;
  std::vector<Signal*> m_signals;
  uhdm::ContAssignCollection* m_contAssigns = nullptr;
  uhdm::ProcessCollection* m_processes = nullptr;
  uhdm::AnyCollection* m_parameters = nullptr;
  uhdm::ParamAssignCollection* m_paramAssigns = nullptr;
  uhdm::ParamAssignCollection* m_origParamAssigns = nullptr;
  uhdm::TaskFuncCollection* m_taskFuncs = nullptr;
  uhdm::TaskFuncDeclCollection* m_taskFuncDecls = nullptr;
  uhdm::AnyCollection* m_assertions = nullptr;
};

}  // namespace SURELOG

#endif /* SURELOG_PORTNETHOLDER_H */
