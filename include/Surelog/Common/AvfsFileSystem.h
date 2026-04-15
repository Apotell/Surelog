/*
 Copyright 2022 chipsalliance

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

#ifndef SURELOG_AVFSFILESYSTEM_H
#define SURELOG_AVFSFILESYSTEM_H
#pragma once

#include <Surelog/Common/PlatformFileSystem.h>

#include <filesystem>
#include <memory>
#include <set>
#include <string_view>

namespace avfs {
class VfsRuntime;
}

namespace SURELOG {
class SymbolTable;

// NOTE(HS): You are assuming all requested files are on disk.
// That is NOT true. This class needs to derive FileSystem and provide
// all the APIs which are primarily just redirection to other registered
// filesystems/mounts.
class AvfsFileSystem final : public PlatformFileSystem {
 public:
  explicit AvfsFileSystem(const std::filesystem::path& workingDir);
  ~AvfsFileSystem() override;

  PathId getProgramFile(std::string_view hint, SymbolTable* symbolTable) override;
  PathId getWorkingDir(std::string_view dir, SymbolTable* symbolTable) override;
  PathId getOutputDir(std::string_view dir, SymbolTable* symbolTable) override;
  PathId getPrecompiledDir(PathId programId, SymbolTable* symbolTable) override;

  bool exists(PathId id) override;
  bool exists(PathId dirId, std::string_view descendant) override;
  bool filesize(PathId fileId, std::streamsize* result) override;

 protected:
  std::istream& openInput(const std::filesystem::path& filepath, std::ios_base::openmode mode) override;
  std::ostream& openOutput(const std::filesystem::path& filepath, std::ios_base::openmode mode) override;

 private:
  void registerMount(const std::filesystem::path& root);
  bool isManagedByAvfs(const std::filesystem::path& path) const;

  std::unique_ptr<avfs::VfsRuntime> m_runtime;
  std::set<std::filesystem::path> m_mountedRoots;
};
}  // namespace SURELOG

#endif  // SURELOG_AVFSFILESYSTEM_H
