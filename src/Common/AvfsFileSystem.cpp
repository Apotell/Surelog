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

#include "Surelog/Common/AvfsFileSystem.h"

#include <avfs/Vfs.h>

#include <filesystem>
#include <ios>
#include <istream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string_view>

namespace SURELOG {

// NOTE(HS): All this is doing is putting a wrapper around platformfilesystem.
// This is not the intended use.
// Assume we have 4 totally independent file system subclasses
// 1. NativeFileSystem
// 2. ZipFileSystem
// 3. NetworkFileSystem
// 4. AzureFileSystem
//
// Each of those implementations are specialized to deal only that specific type.
// VFS keeps a map<mount-handle, FileSystem*>
// Given a request to find a file "$<mount-handle>/abc.txt", VFS has to resolve that
// to a one or many registered file systems and ask for that file. The first one to
// return success wins (or optionally, we could also introduce a priority in case of
// multiple filesystems of the same type).

AvfsFileSystem::AvfsFileSystem(const std::filesystem::path& workingDir)
    : PlatformFileSystem(workingDir), m_runtime(std::make_unique<avfs::VfsRuntime>()) {
  registerMount(workingDir);
}

AvfsFileSystem::~AvfsFileSystem() = default;

PathId AvfsFileSystem::getProgramFile(std::string_view hint, SymbolTable* symbolTable) {
  const PathId result = PlatformFileSystem::getProgramFile(hint, symbolTable);
  registerMount(toPlatformAbsPath(result).parent_path());
  return result;
}

PathId AvfsFileSystem::getWorkingDir(std::string_view dir, SymbolTable* symbolTable) {
  const PathId result = PlatformFileSystem::getWorkingDir(dir, symbolTable);
  registerMount(toPlatformAbsPath(result));
  return result;
}

PathId AvfsFileSystem::getOutputDir(std::string_view dir, SymbolTable* symbolTable) {
  const PathId result = PlatformFileSystem::getOutputDir(dir, symbolTable);
  registerMount(toPlatformAbsPath(result));
  return result;
}

PathId AvfsFileSystem::getPrecompiledDir(PathId programId, SymbolTable* symbolTable) {
  const PathId result = PlatformFileSystem::getPrecompiledDir(programId, symbolTable);
  registerMount(toPlatformAbsPath(result));
  return result;
}

std::istream& AvfsFileSystem::openInput(const std::filesystem::path& filepath, std::ios_base::openmode mode) {
  static_cast<void>(mode);
  if (!filepath.is_absolute()) return m_nullInputStream;

  if (isManagedByAvfs(filepath)) {
    try {
      std::unique_ptr<std::istream> strm;
      if (filepath.extension() == ".gz") {
        auto backingStore = std::make_shared<avfs::PlatformFileSystem>(filepath.parent_path().string());
        avfs::CompressedFileSystem compressed(backingStore);
        strm = compressed.openRead(filepath.filename().generic_string());
      } else {
        strm = m_runtime->openRead(filepath.generic_string());
      }
      std::scoped_lock<std::mutex> lock(m_inputStreamsMutex);
      auto [it, inserted] = m_inputStreams.emplace(std::move(strm));
      static_cast<void>(inserted);
      return *it->get();
    } catch (...) {
    }
  }

  return PlatformFileSystem::openInput(filepath, mode);
}

std::ostream& AvfsFileSystem::openOutput(const std::filesystem::path& filepath, std::ios_base::openmode mode) {
  static_cast<void>(mode);
  if (!filepath.is_absolute()) return m_nullOutputStream;

  const std::filesystem::path parent = filepath.parent_path();
  if (!parent.empty()) {
    registerMount(parent);
  }

  if (isManagedByAvfs(filepath)) {
    try {
      std::unique_ptr<std::ostream> strm = m_runtime->openWrite(filepath.generic_string());
      std::scoped_lock<std::mutex> lock(m_outputStreamsMutex);
      auto [it, inserted] = m_outputStreams.emplace(std::move(strm));
      static_cast<void>(inserted);
      return *it->get();
    } catch (...) {
    }
  }

  return PlatformFileSystem::openOutput(filepath, mode);
}

bool AvfsFileSystem::exists(PathId id) {
  if (!id) return false;

  const std::filesystem::path filepath = toPath(id);
  if (!filepath.empty() && isManagedByAvfs(filepath)) {
    try {
      return m_runtime->exists(filepath.generic_string());
    } catch (...) {
    }
  }

  return PlatformFileSystem::exists(id);
}

bool AvfsFileSystem::exists(PathId dirId, std::string_view descendant) {
  if (!dirId || descendant.empty()) return false;

  std::filesystem::path filepath = toPath(dirId);
  filepath /= descendant;
  if (!filepath.empty() && isManagedByAvfs(filepath)) {
    try {
      return m_runtime->exists(filepath.generic_string());
    } catch (...) {
    }
  }

  return PlatformFileSystem::exists(dirId, descendant);
}

bool AvfsFileSystem::filesize(PathId fileId, std::streamsize* result) {
  if (!fileId) return false;

  const std::filesystem::path filepath = toPath(fileId);
  if (filepath.empty()) return false;

  if (!isManagedByAvfs(filepath) || (filepath.extension() != ".gz")) {
    return PlatformFileSystem::filesize(fileId, result);
  }

  std::istream& strm = openInput(filepath, std::ios_base::in | std::ios_base::binary);
  if (!strm.good()) {
    close(strm);
    return false;
  }

  char buffer[8192];
  std::streamsize length = 0;
  while (strm.read(buffer, sizeof(buffer)) || (strm.gcount() > 0)) {
    length += strm.gcount();
  }

  const bool success = strm.eof() && !strm.bad();
  close(strm);

  if (!success) return false;
  if (result != nullptr) {
    *result = length;
  }
  return true;
}

void AvfsFileSystem::registerMount(const std::filesystem::path& root) {
  if (root.empty()) return;

  const std::filesystem::path normalized = normalize(root);
  if (normalized.empty() || !normalized.is_absolute()) return;
  if (!m_mountedRoots.emplace(normalized).second) return;

  avfs::BackendOptions options;
  options.root = normalized.string();
  m_runtime->mount(normalized.generic_string(), "platform", options);
}

bool AvfsFileSystem::isManagedByAvfs(const std::filesystem::path& path) const {
  const std::filesystem::path normalized = normalize(path);
  for (const std::filesystem::path& root : m_mountedRoots) {
    if (is_subpath(root, normalized)) {
      return true;
    }
  }
  return false;
}

}  // namespace SURELOG
