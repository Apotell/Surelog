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

#include <algorithm>
#include <filesystem>
#include <ios>
#include <istream>
#include <memory>
#include <mutex>
#include <ostream>
#include <regex>
#include <set>
#include <string_view>

namespace SURELOG {

AvfsFileSystem::AvfsFileSystem(const std::filesystem::path& workingDir)
    : m_platform(std::make_unique<PlatformFileSystem>(workingDir)), m_runtime(std::make_unique<avfs::VfsRuntime>()) {
  registerMount(workingDir);
}

AvfsFileSystem::~AvfsFileSystem() {
  {
    std::scoped_lock<std::mutex> lock(m_inputStreamsMutex);
    m_inputStreams.clear();
  }
  {
    std::scoped_lock<std::mutex> lock(m_outputStreamsMutex);
    m_outputStreams.clear();
  }
}

PathId AvfsFileSystem::toPathId(std::string_view path, SymbolTable* symbolTable) {
  return m_platform->toPathId(path, symbolTable);
}

std::string_view AvfsFileSystem::toPath(PathId id) { return FileSystem::toPath(id); }

std::filesystem::path AvfsFileSystem::toPlatformAbsPath(PathId id) { return m_platform->toPlatformAbsPath(id); }

std::filesystem::path AvfsFileSystem::toPlatformRelPath(PathId id) { return m_platform->toPlatformRelPath(id); }

std::pair<std::filesystem::path, std::filesystem::path> AvfsFileSystem::toSplitPlatformPath(PathId id) {
  return m_platform->toSplitPlatformPath(id);
}

std::string AvfsFileSystem::getWorkingDir() { return m_platform->getWorkingDir(); }

std::set<std::string> AvfsFileSystem::getWorkingDirs() { return m_platform->getWorkingDirs(); }

std::istream& AvfsFileSystem::openManagedInput(const std::filesystem::path& filepath, std::ios_base::openmode mode) {
  static_cast<void>(mode);

  try {
    std::unique_ptr<std::istream> stream;
#ifdef SURELOG_WITH_ZLIB
    if (filepath.extension() == ".gz") {
      auto backingStore = std::make_shared<avfs::PlatformFileSystem>(filepath.parent_path().string());
      avfs::CompressedFileSystem compressed(backingStore);
      stream = compressed.openRead(filepath.filename().generic_string());
    } else
#endif
    {
      stream = m_runtime->openRead(filepath.generic_string());
    }

    std::scoped_lock<std::mutex> lock(m_inputStreamsMutex);
    m_inputStreams.emplace_back(std::move(stream));
    return *m_inputStreams.back();
  } catch (...) {
    return m_nullInputStream;
  }
}

std::ostream& AvfsFileSystem::openManagedOutput(const std::filesystem::path& filepath, std::ios_base::openmode mode) {
  static_cast<void>(mode);

  try {
    std::unique_ptr<std::ostream> stream = m_runtime->openWrite(filepath.generic_string());
    std::scoped_lock<std::mutex> lock(m_outputStreamsMutex);
    m_outputStreams.emplace_back(std::move(stream));
    return *m_outputStreams.back();
  } catch (...) {
    return m_nullOutputStream;
  }
}

std::istream& AvfsFileSystem::openInput(PathId fileId, std::ios_base::openmode mode) {
  const std::filesystem::path filepath = toPath(fileId);
  if (filepath.empty()) return m_nullInputStream;

  if (!isManagedByAvfs(filepath)) return m_platform->openInput(fileId, mode);
  return openManagedInput(filepath, mode);
}

bool AvfsFileSystem::close(std::istream& strm) {
  {
    std::scoped_lock<std::mutex> lock(m_inputStreamsMutex);
    auto it = std::find_if(m_inputStreams.begin(), m_inputStreams.end(),
                           [&strm](const auto& stream) { return stream.get() == &strm; });
    if (it != m_inputStreams.end()) {
      m_inputStreams.erase(it);
      return true;
    }
  }
  return m_platform->close(strm);
}

std::ostream& AvfsFileSystem::openOutput(PathId fileId, std::ios_base::openmode mode) {
  const std::filesystem::path filepath = toPath(fileId);
  if (filepath.empty()) return m_nullOutputStream;

  const std::filesystem::path parent = filepath.parent_path();
  if (!parent.empty()) registerMount(parent);

  if (!isManagedByAvfs(filepath)) return m_platform->openOutput(fileId, mode);
  return openManagedOutput(filepath, mode);
}

bool AvfsFileSystem::close(std::ostream& strm) {
  {
    std::scoped_lock<std::mutex> lock(m_outputStreamsMutex);
    auto it = std::find_if(m_outputStreams.begin(), m_outputStreams.end(),
                           [&strm](const auto& stream) { return stream.get() == &strm; });
    if (it != m_outputStreams.end()) {
      m_outputStreams.erase(it);
      return true;
    }
  }
  return m_platform->close(strm);
}

bool AvfsFileSystem::saveContent(PathId fileId, const char* content, std::streamsize length, bool useTemp) {
  if (!fileId) return false;

  const std::filesystem::path filepath = toPath(fileId);
  if (filepath.empty()) return false;

  std::filesystem::path filepath2Write = filepath;
  if (useTemp) filepath2Write += ".tmp";

  PathId outputId = toPathId(filepath2Write.string(), const_cast<SymbolTable*>(fileId.getSymbolTable()));
  if (!outputId) return false;

  bool result = false;
  std::ostream& strm = openOutput(outputId, std::ios_base::out | std::ios_base::binary);
  if (strm.good()) {
    if (length > 0) strm.write(content, length);
    result = strm.good();
  }
  close(strm);

  if (useTemp) {
    if (result) {
      result = rename(outputId, fileId);
    } else {
      remove(outputId);
    }
  }

  return result;
}

bool AvfsFileSystem::addMapping(std::string_view what, std::string_view with) {
  return m_platform->addMapping(what, with);
}

std::string AvfsFileSystem::remap(std::string_view what) { return m_platform->remap(what); }

bool AvfsFileSystem::addWorkingDirectoryCacheEntry(std::string_view prefix, std::string_view suffix) {
  return m_platform->addWorkingDirectoryCacheEntry(prefix, suffix);
}

PathId AvfsFileSystem::getProgramFile(std::string_view hint, SymbolTable* symbolTable) {
  const PathId result = m_platform->getProgramFile(hint, symbolTable);
  if (result) registerMount(toPlatformAbsPath(result).parent_path());
  return result;
}

PathId AvfsFileSystem::getWorkingDir(std::string_view dir, SymbolTable* symbolTable) {
  const PathId result = m_platform->getWorkingDir(dir, symbolTable);
  if (result) registerMount(toPlatformAbsPath(result));
  return result;
}

PathId AvfsFileSystem::getOutputDir(std::string_view dir, SymbolTable* symbolTable) {
  const PathId result = m_platform->getOutputDir(dir, symbolTable);
  if (result) registerMount(toPlatformAbsPath(result));
  return result;
}

PathId AvfsFileSystem::getPrecompiledDir(PathId programId, SymbolTable* symbolTable) {
  const PathId result = m_platform->getPrecompiledDir(programId, symbolTable);
  if (result) registerMount(toPlatformAbsPath(result));
  return result;
}

PathId AvfsFileSystem::getLogFile(std::string_view filename, SymbolTable* symbolTable) {
  const PathId result = m_platform->getLogFile(filename, symbolTable);
  registerPathMount(result, true);
  return result;
}

PathId AvfsFileSystem::getCacheDir(std::string_view dirname, SymbolTable* symbolTable) {
  const PathId result = m_platform->getCacheDir(dirname, symbolTable);
  registerPathMount(result, false);
  return result;
}

PathId AvfsFileSystem::getCompileDir(SymbolTable* symbolTable) {
  const PathId result = m_platform->getCompileDir(symbolTable);
  registerPathMount(result, false);
  return result;
}

PathId AvfsFileSystem::getPpOutputFile(PathId sourceFileId, std::string_view libraryName, SymbolTable* symbolTable) {
  const PathId result = m_platform->getPpOutputFile(sourceFileId, libraryName, symbolTable);
  registerPathMount(result, true);
  return result;
}

PathId AvfsFileSystem::getPpCacheFile(PathId sourceFileId, std::string_view libraryName, bool isPrecompiled,
                                      SymbolTable* symbolTable) {
  const PathId result = m_platform->getPpCacheFile(sourceFileId, libraryName, isPrecompiled, symbolTable);
  registerPathMount(result, true);
  return result;
}

PathId AvfsFileSystem::getParseCacheFile(PathId ppFileId, std::string_view libraryName, bool isPrecompiled,
                                         SymbolTable* symbolTable) {
  const PathId result = m_platform->getParseCacheFile(ppFileId, libraryName, isPrecompiled, symbolTable);
  registerPathMount(result, true);
  return result;
}

PathId AvfsFileSystem::getPythonCacheFile(PathId sourceFileId, std::string_view libraryName,
                                          SymbolTable* symbolTable) {
  const PathId result = m_platform->getPythonCacheFile(sourceFileId, libraryName, symbolTable);
  registerPathMount(result, true);
  return result;
}

PathId AvfsFileSystem::getPpMultiprocessingDir(SymbolTable* symbolTable) {
  const PathId result = m_platform->getPpMultiprocessingDir(symbolTable);
  registerPathMount(result, false);
  return result;
}

PathId AvfsFileSystem::getParserMultiprocessingDir(SymbolTable* symbolTable) {
  const PathId result = m_platform->getParserMultiprocessingDir(symbolTable);
  registerPathMount(result, false);
  return result;
}

PathId AvfsFileSystem::getChunkFile(PathId ppFileId, int32_t chunkIndex, SymbolTable* symbolTable) {
  const PathId result = m_platform->getChunkFile(ppFileId, chunkIndex, symbolTable);
  registerPathMount(result, true);
  return result;
}

PathId AvfsFileSystem::getCheckerDir(SymbolTable* symbolTable) {
  const PathId result = m_platform->getCheckerDir(symbolTable);
  registerPathMount(result, false);
  return result;
}

PathId AvfsFileSystem::getCheckerFile(PathId uhdmFileId, SymbolTable* symbolTable) {
  const PathId result = m_platform->getCheckerFile(uhdmFileId, symbolTable);
  registerPathMount(result, true);
  return result;
}

PathId AvfsFileSystem::getCheckerHtmlFile(PathId uhdmFileId, SymbolTable* symbolTable) {
  const PathId result = m_platform->getCheckerHtmlFile(uhdmFileId, symbolTable);
  registerPathMount(result, true);
  return result;
}

PathId AvfsFileSystem::getCheckerHtmlFile(PathId uhdmFileId, int32_t index, SymbolTable* symbolTable) {
  const PathId result = m_platform->getCheckerHtmlFile(uhdmFileId, index, symbolTable);
  registerPathMount(result, true);
  return result;
}

PathId AvfsFileSystem::getOutputUhdmFile(SymbolTable* symbolTable) {
  const PathId result = m_platform->getOutputUhdmFile(symbolTable);
  registerPathMount(result, true);
  return result;
}

bool AvfsFileSystem::rename(PathId whatId, PathId toId) {
  const bool result = m_platform->rename(whatId, toId);
  if (result) registerPathMount(toId, true);
  return result;
}

bool AvfsFileSystem::remove(PathId fileId) { return m_platform->remove(fileId); }

bool AvfsFileSystem::mkdir(PathId dirId) {
  const bool result = m_platform->mkdir(dirId);
  if (result) registerPathMount(dirId, false);
  return result;
}

bool AvfsFileSystem::rmdir(PathId dirId) { return m_platform->rmdir(dirId); }

bool AvfsFileSystem::mkdirs(PathId dirId) {
  const bool result = m_platform->mkdirs(dirId);
  if (result) registerPathMount(dirId, false);
  return result;
}

bool AvfsFileSystem::rmtree(PathId dirId) { return m_platform->rmtree(dirId); }

bool AvfsFileSystem::exists(PathId id) {
  if (!id) return false;

  const std::filesystem::path filepath = toPath(id);
  if (!filepath.empty() && isManagedByAvfs(filepath)) {
    try {
      return m_runtime->exists(filepath.generic_string());
    } catch (...) {
    }
  }

  return m_platform->exists(id);
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

  return m_platform->exists(dirId, descendant);
}

bool AvfsFileSystem::isDirectory(PathId id) { return m_platform->isDirectory(id); }

bool AvfsFileSystem::isRegularFile(PathId id) { return m_platform->isRegularFile(id); }

bool AvfsFileSystem::filesize(PathId fileId, std::streamsize* result) {
  if (!fileId) return false;

  const std::filesystem::path filepath = toPath(fileId);
  if (filepath.empty()) return false;

#ifdef SURELOG_WITH_ZLIB
  if (isManagedByAvfs(filepath) && (filepath.extension() == ".gz")) {
    std::istream& strm = openManagedInput(filepath, std::ios_base::in | std::ios_base::binary);
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

    if (result != nullptr) *result = length;
    return true;
  }
#endif

  return m_platform->filesize(fileId, result);
}

std::filesystem::file_time_type AvfsFileSystem::modtime(PathId fileId,
                                                        std::filesystem::file_time_type defaultOnFail) {
  return m_platform->modtime(fileId, defaultOnFail);
}

PathId AvfsFileSystem::locate(std::string_view name, const PathIdVector& directories, SymbolTable* symbolTable) {
  return m_platform->locate(name, directories, symbolTable);
}

PathIdVector& AvfsFileSystem::collect(PathId dirId, SymbolTable* symbolTable, PathIdVector& container) {
  return m_platform->collect(dirId, symbolTable, container);
}

PathIdVector& AvfsFileSystem::collect(PathId dirId, std::string_view extension, SymbolTable* symbolTable,
                                      PathIdVector& container) {
  return m_platform->collect(dirId, extension, symbolTable, container);
}

PathIdVector& AvfsFileSystem::matching(PathId dirId, std::string_view pattern, SymbolTable* symbolTable,
                                       PathIdVector& container) {
  return m_platform->matching(dirId, pattern, symbolTable, container);
}

PathIdVector& AvfsFileSystem::matching(PathId dirId, const std::regex& pattern, SymbolTable* symbolTable,
                                       PathIdVector& container) {
  return m_platform->matching(dirId, pattern, symbolTable, container);
}

PathId AvfsFileSystem::getChild(PathId id, std::string_view name, SymbolTable* symbolTable) {
  return m_platform->getChild(id, name, symbolTable);
}

PathId AvfsFileSystem::getSibling(PathId id, std::string_view name, SymbolTable* symbolTable) {
  return m_platform->getSibling(id, name, symbolTable);
}

PathId AvfsFileSystem::getParent(PathId id, SymbolTable* symbolTable) { return m_platform->getParent(id, symbolTable); }

std::pair<SymbolId, std::string_view> AvfsFileSystem::getLeaf(PathId id, SymbolTable* symbolTable) {
  return m_platform->getLeaf(id, symbolTable);
}

std::pair<SymbolId, std::string_view> AvfsFileSystem::getType(PathId id, SymbolTable* symbolTable) {
  return m_platform->getType(id, symbolTable);
}

PathId AvfsFileSystem::copy(PathId id, SymbolTable* toSymbolTable) { return FileSystem::copy(id, toSymbolTable); }

void AvfsFileSystem::printConfiguration(std::ostream& out) {
  m_platform->printConfiguration(out);
  out << "avfs mounts:" << std::endl;
  std::vector<std::filesystem::path> mountedRoots;
  {
    std::scoped_lock<std::mutex> lock(m_mountsMutex);
    mountedRoots = m_mountedRoots;
  }
  for (const auto& root : mountedRoots) {
    out << "  " << root.string() << std::endl;
  }
}

void AvfsFileSystem::registerPathMount(PathId id, bool useParentPath) {
  if (!id) return;

  std::filesystem::path mountPath = toPlatformAbsPath(id);
  if (useParentPath) mountPath = mountPath.parent_path();
  registerMount(mountPath);
}

void AvfsFileSystem::registerMount(const std::filesystem::path& root) {
  if (root.empty()) return;

  const std::filesystem::path normalized = PlatformFileSystem::normalize(root);
  if (normalized.empty() || !normalized.is_absolute()) return;

  {
    std::scoped_lock<std::mutex> lock(m_mountsMutex);
    if (std::find(m_mountedRoots.begin(), m_mountedRoots.end(), normalized) != m_mountedRoots.end()) return;
    m_mountedRoots.emplace_back(normalized);
  }

  avfs::BackendOptions options;
  options.root = normalized.string();
  m_runtime->mount(normalized.generic_string(), "platform", options);
}

bool AvfsFileSystem::isManagedByAvfs(const std::filesystem::path& path) const {
  const std::filesystem::path normalized = PlatformFileSystem::normalize(path);
  std::scoped_lock<std::mutex> lock(m_mountsMutex);
  for (const std::filesystem::path& root : m_mountedRoots) {
    if (PlatformFileSystem::is_subpath(root, normalized)) return true;
  }
  return false;
}

}  // namespace SURELOG
