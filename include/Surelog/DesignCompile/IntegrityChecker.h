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
 * File:   IntegrityChecker.h
 * Author: hs
 *
 * Created on August 10, 2024, 00:00 AM
 */

#ifndef SURELOG_INTEGRITYCHECKER_H
#define SURELOG_INTEGRITYCHECKER_H
#pragma once

#include <uhdm/uhdm_forward_decl.h>
#include <uhdm/UhdmListener.h>

#include <set>
#include <string_view>
#include <vector>

namespace SURELOG {

class ErrorContainer;
class FileSystem;
class SymbolTable;

class IntegrityChecker final : protected UHDM::UhdmListener {
 public:
  IntegrityChecker(FileSystem* fileSystem, SymbolTable* symbolTable,
                   ErrorContainer* errorContainer);

  void check(const UHDM::design* const object);
  void check(const std::vector<const UHDM::design*>& objects);

 private:
  void enterAny(const UHDM::any* const object) final;

  bool isBuiltPackageOnStack(const UHDM::any* const object) const;

  template <typename T>
  void reportAmbigiousMembership(const std::vector<T*>* const collection,
                                 const T* const object) const;

  template <typename T>
  void reportDuplicates(const UHDM::any* const object,
                        const std::vector<T*>* const collection,
                        std::string_view name) const;

  void reportInvalidLocation(const UHDM::any* const object) const;

  void reportMissingLocation(const UHDM::any* const object) const;

  static bool isImplicitFunctionReturnType(const UHDM::any* const object);

  static std::string_view stripDecorations(std::string_view name);

  static bool areNamedSame(const UHDM::any* const object,
                           const UHDM::any* const actual);

  void reportInvalidNames(const UHDM::any* const object) const;

  void reportInvalidFile(const UHDM::any* const object) const;

  void reportNullActual(const UHDM::any* const object) const;

 private:
  FileSystem* const m_fileSystem = nullptr;
  SymbolTable* const m_symbolTable = nullptr;
  ErrorContainer* const m_errorContainer = nullptr;

  typedef std::set<UHDM::UHDM_OBJECT_TYPE> object_type_set_t;
  const object_type_set_t m_acceptedObjectsWithInvalidLocations;
};

};  // namespace SURELOG

#endif /* SURELOG_INTEGRITYCHECKER_H */
