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
#include <uhdm/uhdm_forward_decl.h>

#include <algorithm>
#include <vector>

namespace SURELOG {

class Signal;

class PortNetHolder {
 public:
  virtual ~PortNetHolder() {}  // virtual as used as interface

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

  std::vector<UHDM::cont_assign*>* getContAssigns() const {
    return m_contAssigns;
  }

  void setContAssigns(std::vector<UHDM::cont_assign*>* cont_assigns) {
    m_contAssigns = cont_assigns;
  }

  std::vector<UHDM::process_stmt*>* getProcesses() const { return m_processes; }
  void setProcesses(std::vector<UHDM::process_stmt*>* processes) {
    m_processes = processes;
  }

  std::vector<UHDM::any*>* getParameters() const { return m_parameters; }
  void setParameters(std::vector<UHDM::any*>* parameters) {
    m_parameters = parameters;
  }

  std::vector<UHDM::any*>* getAssertions() const { return m_assertions; }
  void setAssertions(std::vector<UHDM::any*>* assertions) {
    m_assertions = assertions;
  }

  std::vector<UHDM::param_assign*>* getParam_assigns() const {
    return m_param_assigns;
  }
  void setParam_assigns(std::vector<UHDM::param_assign*>* param_assigns) {
    m_param_assigns = param_assigns;
  }

  std::vector<UHDM::param_assign*>* getOrigParam_assigns() const {
    return m_orig_param_assigns;
  }
  void setOrigParam_assigns(std::vector<UHDM::param_assign*>* param_assigns) {
    m_orig_param_assigns = param_assigns;
  }

  std::vector<UHDM::task_func*>* getTask_funcs() const { return m_task_funcs; }

  void setTask_funcs(std::vector<UHDM::task_func*>* task_funcs) {
    m_task_funcs = task_funcs;
  }

  std::vector<UHDM::task_func_decl*>* getTask_func_decls() const {
    return m_task_func_decls;
  }

  void setTask_func_decls(std::vector<UHDM::task_func_decl*>* task_func_decls) {
    m_task_func_decls = task_func_decls;
  }

 protected:
  std::vector<Signal*> m_ports;
  std::vector<Signal*> m_signals;
  std::vector<UHDM::cont_assign*>* m_contAssigns = nullptr;
  std::vector<UHDM::process_stmt*>* m_processes = nullptr;
  std::vector<UHDM::any*>* m_parameters = nullptr;
  std::vector<UHDM::param_assign*>* m_param_assigns = nullptr;
  std::vector<UHDM::param_assign*>* m_orig_param_assigns = nullptr;
  std::vector<UHDM::task_func*>* m_task_funcs = nullptr;
  std::vector<UHDM::task_func_decl*>* m_task_func_decls = nullptr;
  std::vector<UHDM::any*>* m_assertions = nullptr;
};

}  // namespace SURELOG

#endif /* SURELOG_PORTNETHOLDER_H */
