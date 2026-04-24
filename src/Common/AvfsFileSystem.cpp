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

#include "Surelog/SourceCompile/SymbolTable.h"

namespace SURELOG {

namespace {

std::string_view getMountVariableName(std::string_view path) {
  if (path.empty() || (path.front() != '$')) return {};

  // REVIEW(HS): Use std::string::find_first_of("\\\/") ??

  size_t separator = 1;
  while ((separator < path.size()) && (path[separator] != '/') && (path[separator] != '\\')) {
    ++separator;
  }

  return path.substr(1, separator - 1);
}

}  // namespace

AvfsFileSystem::AvfsFileSystem(const std::filesystem::path& workingDir)
    : m_platform(std::make_unique<PlatformFileSystem>(workingDir)), m_runtime(std::make_unique<avfs::VfsRuntime>()) {}

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

bool AvfsFileSystem::registerMount(std::string_view variableName, const std::filesystem::path& root) {
  const std::string normalizedVariableName = normalizeVariableName(variableName);
  const std::filesystem::path normalizedRoot = PlatformFileSystem::normalize(root);
  const std::filesystem::path comparableRoot = PlatformFileSystem::normalizeForComparison(normalizedRoot);
  if (normalizedVariableName.empty() || normalizedRoot.empty() || !normalizedRoot.is_absolute()) return false;

  MountRegistration mount{normalizedVariableName, std::string("$").append(normalizedVariableName),
                          std::string("/").append(normalizedVariableName), normalizedRoot};
  {
    std::scoped_lock<std::mutex> lock(m_mountsMutex);
    for (const MountRegistration& current : m_mounts) {
      const std::filesystem::path comparableCurrentRoot = PlatformFileSystem::normalizeForComparison(current.m_root);
      if (current.m_variableName == mount.m_variableName) return comparableCurrentRoot == comparableRoot;
      if (comparableCurrentRoot == comparableRoot) return current.m_variableName == mount.m_variableName;
    }
    m_mounts.emplace_back(mount);
  }

  try {
    avfs::BackendOptions options;
    options.root = normalizedRoot.string();
    m_runtime->mountVariable(mount.m_variableName, mount.m_logicalMountPoint, "platform", options);
  } catch (...) {
    std::scoped_lock<std::mutex> lock(m_mountsMutex);
    auto it = std::find_if(m_mounts.begin(), m_mounts.end(), [&](const MountRegistration& current) {
      return (current.m_variableName == mount.m_variableName) && (current.m_root == mount.m_root);
    });
    if (it != m_mounts.end()) m_mounts.erase(it);
    return false;
  }

  return true;
}

std::vector<AvfsFileSystem::MountInfo> AvfsFileSystem::getMounts() const {
  std::vector<MountInfo> mounts;
  std::scoped_lock<std::mutex> lock(m_mountsMutex);
  mounts.reserve(m_mounts.size());
  for (const MountRegistration& mount : m_mounts) {
    mounts.emplace_back(MountInfo{mount.m_variableName, mount.m_root});
  }
  return mounts;
}

std::string AvfsFileSystem::normalizeVariablePath(std::string_view path) {
  if (path.empty()) return {};
  if (path.front() != '$') return std::string(path);

  const std::string_view variableName = getMountVariableName(path);
  if (variableName.empty()) return {};

  std::string normalized = std::string("$").append(normalizeVariableName(variableName));
  const size_t separator = 1 + variableName.size();
  if (separator >= path.size()) return normalized;

  const std::string suffix = avfs::VirtualFileSystem::normalizeRelativePath(std::string(path.substr(separator)));
  if (!suffix.empty()) {
    normalized.append("/").append(suffix);
  }

  return normalized;
}

std::string AvfsFileSystem::normalizeVariableName(std::string_view variableName) {
  if (variableName.empty()) return {};
  if (variableName.front() == '$') variableName.remove_prefix(1);
  return variableName.empty() ? std::string() : std::string(variableName);
}

// REVIEW(HS): We should enforce that all paths are managed. There shouldn't be anything that uses native paths.
bool AvfsFileSystem::isManagedPath(std::string_view path) { return !path.empty() && (path.front() == '$'); }

// REVIEW(HS): It is expected that all mount points will be added at the start of the process. We don't support
// changing configuration during processing. This would avoid need for mutex/lock in every API call.
const AvfsFileSystem::MountRegistration* AvfsFileSystem::findMountByVariable(std::string_view variableName) const {
  const std::string normalized = normalizeVariableName(variableName);
  std::scoped_lock<std::mutex> lock(m_mountsMutex);
  for (const MountRegistration& mount : m_mounts) {
    if (mount.m_variableName == normalized) return &mount;
  }
  return nullptr;
}

// REVIEW(HS): With this change there is no concept of platformpath. All use-cases will only be virtual path.
const AvfsFileSystem::MountRegistration* AvfsFileSystem::findMountForPlatformPath(
    const std::filesystem::path& path) const {
  const std::filesystem::path normalized = PlatformFileSystem::normalize(path);

  std::scoped_lock<std::mutex> lock(m_mountsMutex);
  const MountRegistration* best = nullptr;
  for (const MountRegistration& mount : m_mounts) {
    if (!PlatformFileSystem::is_subpath(mount.m_root, normalized)) continue;
    const std::filesystem::path comparableRoot = PlatformFileSystem::normalizeForComparison(mount.m_root);
    if ((best == nullptr) ||
        (comparableRoot.string().size() > PlatformFileSystem::normalizeForComparison(best->m_root).string().size())) {
      best = &mount;
    }
  }

  return best;
}

std::string AvfsFileSystem::makeManagedPath(const MountRegistration& mount, const std::filesystem::path& path) const {
  const std::filesystem::path normalized = PlatformFileSystem::normalize(path);
  if (normalized == mount.m_root) return mount.m_variablePrefix;

  const std::filesystem::path relative = normalized.lexically_relative(mount.m_root);
  const std::string suffix = relative.generic_string();
  return suffix.empty() || (suffix == ".") ? mount.m_variablePrefix
                                           : std::string(mount.m_variablePrefix).append("/").append(suffix);
}

std::filesystem::path AvfsFileSystem::resolveManagedPath(std::string_view path) const {
  if (!isManagedPath(path)) return {};

  const std::string normalized = normalizeVariablePath(path);
  const std::string_view variableName = getMountVariableName(normalized);
  const MountRegistration* mount = findMountByVariable(variableName);
  if (mount == nullptr) return {};

  std::filesystem::path resolved = mount->m_root;
  const size_t separator = 1 + variableName.size();
  if (separator < normalized.size()) {
    const std::string_view suffix = normalized.substr(separator + 1);
    if (!suffix.empty()) resolved /= std::filesystem::path(std::string(suffix));
  }

  return PlatformFileSystem::normalize(resolved);
}

std::filesystem::path AvfsFileSystem::resolveInputPath(std::string_view path) const {
  if (isManagedPath(path)) return resolveManagedPath(path);
  return PlatformFileSystem::normalize(std::filesystem::path(path));
}

PathId AvfsFileSystem::translateToPlatformPathId(PathId id) const {
  if (!id) return BadPathId;

  const std::filesystem::path resolved = const_cast<AvfsFileSystem*>(this)->toPlatformAbsPath(id);
  if (resolved.empty() || resolved.is_relative()) return BadPathId;

  return m_platform->toPathId(resolved.string(), const_cast<SymbolTable*>(id.getSymbolTable()));
}

PathId AvfsFileSystem::translateFromPlatformPathId(PathId id, SymbolTable* symbolTable) const {
  if (!id) return BadPathId;

  SymbolTable* const targetSymbols =
      (symbolTable != nullptr) ? symbolTable : const_cast<SymbolTable*>(id.getSymbolTable());
  const std::filesystem::path resolved = m_platform->toPlatformAbsPath(id);
  return resolved.empty() ? BadPathId
                          : const_cast<AvfsFileSystem*>(this)->toPathId(resolved.string(), targetSymbols);
}

// REVIEW(HS): PlatformFileSystem will get entirely deprecated with this change. Don't depend on it.
PathId AvfsFileSystem::toPathId(std::string_view path, SymbolTable* symbolTable) {
  if (path.empty()) return BadPathId;

  std::string storedPath;
  if (isManagedPath(path)) {
    storedPath = normalizeVariablePath(path);
  } else {
    const std::filesystem::path normalized = PlatformFileSystem::normalize(std::filesystem::path(path));
    if (normalized.empty() || normalized.is_relative()) return BadPathId;

    if (const MountRegistration* mount = findMountForPlatformPath(normalized)) {
      storedPath = makeManagedPath(*mount, normalized);
    } else {
      storedPath = normalized.string();
      // REVIEW(HS): Above statement would make a path like "$DataDir\abc.xyz".
      // We want to enforce/support only forward slashes. All paths should use only forward slashes,
      // expect for communication between AVFS and NativeFileSystem.
    }
  }

  if (storedPath.empty()) return BadPathId;

  auto [symbolId, symbol] = symbolTable->add(storedPath);
  return PathId(symbolTable, (RawSymbolId)symbolId, symbol);
}

std::string_view AvfsFileSystem::toPath(PathId id) { return FileSystem::toPath(id); }

// REVIEW(HS): Remove this API! Nothing outside of the AVFS system should be going
// directly to the native platform. There shouldn't be a need to resolve any id to
// platform specific path.
std::filesystem::path AvfsFileSystem::toPlatformAbsPath(PathId id) {
  const std::string_view storedPath = toPath(id);
  if (storedPath.empty()) return {};
  return isManagedPath(storedPath) ? resolveManagedPath(storedPath) : m_platform->toPlatformAbsPath(id);
}

// REVIEW(HS): Unnecessary API. There won't be a notion of "Platform Path"
std::filesystem::path AvfsFileSystem::toPlatformRelPath(PathId id) { return toSplitPlatformPath(id).second; }

std::pair<std::filesystem::path, std::filesystem::path> AvfsFileSystem::toSplitPlatformPath(PathId id) {
  const PathId platformId = translateToPlatformPathId(id);
  return platformId ? m_platform->toSplitPlatformPath(platformId) : std::pair<std::filesystem::path, std::filesystem::path>();
}

// REVIEW(HS): Unnecessary API. There won't be a notion of "Platform Path"
// Even it did exist, it can't get it from m_platform.
// It has to be a virtual path.
std::string AvfsFileSystem::getWorkingDir() { return m_platform->getWorkingDir(); }

std::set<std::string> AvfsFileSystem::getWorkingDirs() { return m_platform->getWorkingDirs(); }

std::istream& AvfsFileSystem::openManagedInput(std::string_view filepath, std::ios_base::openmode mode) {
  static_cast<void>(mode);

  try {
    std::unique_ptr<std::istream> stream;
#ifdef SURELOG_WITH_ZLIB
    const std::filesystem::path resolved = resolveManagedPath(filepath);
    if (!resolved.empty() && (resolved.extension() == ".gz")) {
      auto backingStore = std::make_shared<avfs::PlatformFileSystem>(resolved.parent_path().string());
      avfs::CompressedFileSystem compressed(backingStore);
      stream = compressed.openRead(resolved.filename().generic_string());
    } else
#endif
    {
      stream = m_runtime->openRead(std::string(filepath));
    }

    if (!stream) return m_nullInputStream;

    std::scoped_lock<std::mutex> lock(m_inputStreamsMutex);
    m_inputStreams.emplace_back(std::move(stream));
    return *m_inputStreams.back();
  } catch (...) {
    return m_nullInputStream;
  }
}

std::ostream& AvfsFileSystem::openManagedOutput(std::string_view filepath, std::ios_base::openmode mode) {
  static_cast<void>(mode);

  try {
    std::unique_ptr<std::ostream> stream = m_runtime->openWrite(std::string(filepath));
    if (!stream) return m_nullOutputStream;

    std::scoped_lock<std::mutex> lock(m_outputStreamsMutex);
    m_outputStreams.emplace_back(std::move(stream));
    return *m_outputStreams.back();
  } catch (...) {
    return m_nullOutputStream;
  }
}

std::istream& AvfsFileSystem::openInput(PathId fileId, std::ios_base::openmode mode) {
  const std::string_view filepath = toPath(fileId);
  if (filepath.empty()) return m_nullInputStream;

  // REVIEW(HS): This will never happen. All paths are managed or virtual.
  if (!isManagedPath(filepath)) {
    const PathId platformId = translateToPlatformPathId(fileId);
    return platformId ? m_platform->openInput(platformId, mode) : m_nullInputStream;
  }

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
  const std::string_view filepath = toPath(fileId);
  if (filepath.empty()) return m_nullOutputStream;

  // REVIEW(HS): This will never happen. All paths are managed or virtual.
  if (!isManagedPath(filepath)) {
    const PathId platformId = translateToPlatformPathId(fileId);
    return platformId ? m_platform->openOutput(platformId, mode) : m_nullOutputStream;
  }

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

  PathId outputId = fileId;
  if (useTemp) {
    std::string filepath(toPath(fileId));
    if (filepath.empty()) return false;
    filepath.append(".tmp");
    outputId = toPathId(filepath, const_cast<SymbolTable*>(fileId.getSymbolTable()));
    if (!outputId) return false;
  }

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

bool AvfsFileSystem::addMapping(std::string_view what, std::string_view with) { return m_platform->addMapping(what, with); }

std::string AvfsFileSystem::remap(std::string_view what) { return m_platform->remap(what); }

bool AvfsFileSystem::addWorkingDirectoryCacheEntry(std::string_view prefix, std::string_view suffix) {
  return m_platform->addWorkingDirectoryCacheEntry(prefix, suffix);
}

PathId AvfsFileSystem::getProgramFile(std::string_view hint, SymbolTable* symbolTable) {
  const std::filesystem::path resolvedHint = resolveInputPath(hint);
  const PathId result = m_platform->getProgramFile(resolvedHint.empty() ? hint : resolvedHint.string(), symbolTable);
  return translateFromPlatformPathId(result, symbolTable);
}

PathId AvfsFileSystem::getWorkingDir(std::string_view dir, SymbolTable* symbolTable) {
  const std::filesystem::path resolved = resolveInputPath(dir);
  if (resolved.empty() || !resolved.is_absolute()) return BadPathId;
  return translateFromPlatformPathId(m_platform->getWorkingDir(resolved.string(), symbolTable), symbolTable);
}

PathId AvfsFileSystem::getOutputDir(std::string_view dir, SymbolTable* symbolTable) {
  const std::filesystem::path resolved = resolveInputPath(dir);
  if (resolved.empty() || !resolved.is_absolute()) return BadPathId;
  return translateFromPlatformPathId(m_platform->getOutputDir(resolved.string(), symbolTable), symbolTable);
}

PathId AvfsFileSystem::getPrecompiledDir(PathId programId, SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getPrecompiledDir(translateToPlatformPathId(programId), symbolTable),
                                     symbolTable);
}

PathId AvfsFileSystem::getLogFile(std::string_view filename, SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getLogFile(filename, symbolTable), symbolTable);
}

PathId AvfsFileSystem::getCacheDir(std::string_view dirname, SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getCacheDir(dirname, symbolTable), symbolTable);
}

PathId AvfsFileSystem::getCompileDir(SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getCompileDir(symbolTable), symbolTable);
}

PathId AvfsFileSystem::getPpOutputFile(PathId sourceFileId, std::string_view libraryName, SymbolTable* symbolTable) {
  return translateFromPlatformPathId(
      m_platform->getPpOutputFile(translateToPlatformPathId(sourceFileId), libraryName, symbolTable), symbolTable);
}

PathId AvfsFileSystem::getPpCacheFile(PathId sourceFileId, std::string_view libraryName, bool isPrecompiled,
                                      SymbolTable* symbolTable) {
  return translateFromPlatformPathId(
      m_platform->getPpCacheFile(translateToPlatformPathId(sourceFileId), libraryName, isPrecompiled, symbolTable),
      symbolTable);
}

PathId AvfsFileSystem::getParseCacheFile(PathId ppFileId, std::string_view libraryName, bool isPrecompiled,
                                         SymbolTable* symbolTable) {
  return translateFromPlatformPathId(
      m_platform->getParseCacheFile(translateToPlatformPathId(ppFileId), libraryName, isPrecompiled, symbolTable),
      symbolTable);
}

PathId AvfsFileSystem::getPythonCacheFile(PathId sourceFileId, std::string_view libraryName,
                                          SymbolTable* symbolTable) {
  return translateFromPlatformPathId(
      m_platform->getPythonCacheFile(translateToPlatformPathId(sourceFileId), libraryName, symbolTable), symbolTable);
}

PathId AvfsFileSystem::getPpMultiprocessingDir(SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getPpMultiprocessingDir(symbolTable), symbolTable);
}

PathId AvfsFileSystem::getParserMultiprocessingDir(SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getParserMultiprocessingDir(symbolTable), symbolTable);
}

PathId AvfsFileSystem::getChunkFile(PathId ppFileId, int32_t chunkIndex, SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getChunkFile(translateToPlatformPathId(ppFileId), chunkIndex, symbolTable),
                                     symbolTable);
}

PathId AvfsFileSystem::getCheckerDir(SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getCheckerDir(symbolTable), symbolTable);
}

PathId AvfsFileSystem::getCheckerFile(PathId uhdmFileId, SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getCheckerFile(translateToPlatformPathId(uhdmFileId), symbolTable),
                                     symbolTable);
}

PathId AvfsFileSystem::getCheckerHtmlFile(PathId uhdmFileId, SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getCheckerHtmlFile(translateToPlatformPathId(uhdmFileId), symbolTable),
                                     symbolTable);
}

PathId AvfsFileSystem::getCheckerHtmlFile(PathId uhdmFileId, int32_t index, SymbolTable* symbolTable) {
  return translateFromPlatformPathId(
      m_platform->getCheckerHtmlFile(translateToPlatformPathId(uhdmFileId), index, symbolTable), symbolTable);
}

PathId AvfsFileSystem::getOutputUhdmFile(SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getOutputUhdmFile(symbolTable), symbolTable);
}

bool AvfsFileSystem::rename(PathId whatId, PathId toId) {
  const PathId platformWhatId = translateToPlatformPathId(whatId);
  const PathId platformToId = translateToPlatformPathId(toId);
  return platformWhatId && platformToId && m_platform->rename(platformWhatId, platformToId);
}

bool AvfsFileSystem::remove(PathId fileId) {
  const PathId platformId = translateToPlatformPathId(fileId);
  return platformId && m_platform->remove(platformId);
}

bool AvfsFileSystem::mkdir(PathId dirId) {
  const PathId platformId = translateToPlatformPathId(dirId);
  return platformId && m_platform->mkdir(platformId);
}

bool AvfsFileSystem::rmdir(PathId dirId) {
  const PathId platformId = translateToPlatformPathId(dirId);
  return platformId && m_platform->rmdir(platformId);
}

bool AvfsFileSystem::mkdirs(PathId dirId) {
  const PathId platformId = translateToPlatformPathId(dirId);
  return platformId && m_platform->mkdirs(platformId);
}

bool AvfsFileSystem::rmtree(PathId dirId) {
  const PathId platformId = translateToPlatformPathId(dirId);
  return platformId && m_platform->rmtree(platformId);
}

bool AvfsFileSystem::exists(PathId id) {
  if (!id) return false;

  const std::string_view filepath = toPath(id);
  if (filepath.empty()) return false;

  if (isManagedPath(filepath)) {
    try {
      return m_runtime->exists(std::string(filepath));
    } catch (...) {
      return false;
    }
  }

  const PathId platformId = translateToPlatformPathId(id);
  return platformId && m_platform->exists(platformId);
}

bool AvfsFileSystem::exists(PathId dirId, std::string_view descendant) {
  if (!dirId || descendant.empty()) return false;

  const PathId platformDirId = translateToPlatformPathId(dirId);
  return platformDirId && m_platform->exists(platformDirId, descendant);
}

bool AvfsFileSystem::isDirectory(PathId id) {
  const PathId platformId = translateToPlatformPathId(id);
  return platformId && m_platform->isDirectory(platformId);
}

bool AvfsFileSystem::isRegularFile(PathId id) {
  const PathId platformId = translateToPlatformPathId(id);
  return platformId && m_platform->isRegularFile(platformId);
}

bool AvfsFileSystem::filesize(PathId fileId, std::streamsize* result) {
  if (!fileId) return false;

  const std::string_view filepath = toPath(fileId);
  if (filepath.empty()) return false;

#ifdef SURELOG_WITH_ZLIB
  if (isManagedPath(filepath)) {
    const std::filesystem::path resolved = resolveManagedPath(filepath);
    if (!resolved.empty() && (resolved.extension() == ".gz")) {
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
  }
#endif

  const PathId platformId = translateToPlatformPathId(fileId);
  return platformId && m_platform->filesize(platformId, result);
}

std::filesystem::file_time_type AvfsFileSystem::modtime(PathId fileId,
                                                        std::filesystem::file_time_type defaultOnFail) {
  const PathId platformId = translateToPlatformPathId(fileId);
  return platformId ? m_platform->modtime(platformId, defaultOnFail) : defaultOnFail;
}

PathId AvfsFileSystem::locate(std::string_view name, const PathIdVector& directories, SymbolTable* symbolTable) {
  PathIdVector translatedDirectories;
  translatedDirectories.reserve(directories.size());
  std::transform(directories.begin(), directories.end(), std::back_inserter(translatedDirectories),
                 [this](const PathId& dirId) { return translateToPlatformPathId(dirId); });
  return translateFromPlatformPathId(m_platform->locate(name, translatedDirectories, symbolTable), symbolTable);
}

PathIdVector& AvfsFileSystem::collect(PathId dirId, SymbolTable* symbolTable, PathIdVector& container) {
  PathIdVector translated;
  const PathId platformDirId = translateToPlatformPathId(dirId);
  if (!platformDirId) return container;

  m_platform->collect(platformDirId, symbolTable, translated);
  std::transform(translated.begin(), translated.end(), std::back_inserter(container),
                 [this, symbolTable](const PathId& id) { return translateFromPlatformPathId(id, symbolTable); });
  return container;
}

PathIdVector& AvfsFileSystem::collect(PathId dirId, std::string_view extension, SymbolTable* symbolTable,
                                      PathIdVector& container) {
  PathIdVector translated;
  const PathId platformDirId = translateToPlatformPathId(dirId);
  if (!platformDirId) return container;

  m_platform->collect(platformDirId, extension, symbolTable, translated);
  std::transform(translated.begin(), translated.end(), std::back_inserter(container),
                 [this, symbolTable](const PathId& id) { return translateFromPlatformPathId(id, symbolTable); });
  return container;
}

PathIdVector& AvfsFileSystem::matching(PathId dirId, std::string_view pattern, SymbolTable* symbolTable,
                                       PathIdVector& container) {
  PathIdVector translated;
  const PathId platformDirId = translateToPlatformPathId(dirId);
  if (!platformDirId) return container;

  m_platform->matching(platformDirId, pattern, symbolTable, translated);
  std::transform(translated.begin(), translated.end(), std::back_inserter(container),
                 [this, symbolTable](const PathId& id) { return translateFromPlatformPathId(id, symbolTable); });
  return container;
}

PathIdVector& AvfsFileSystem::matching(PathId dirId, const std::regex& pattern, SymbolTable* symbolTable,
                                       PathIdVector& container) {
  PathIdVector translated;
  const PathId platformDirId = translateToPlatformPathId(dirId);
  if (!platformDirId) return container;

  m_platform->matching(platformDirId, pattern, symbolTable, translated);
  std::transform(translated.begin(), translated.end(), std::back_inserter(container),
                 [this, symbolTable](const PathId& id) { return translateFromPlatformPathId(id, symbolTable); });
  return container;
}

PathId AvfsFileSystem::getChild(PathId id, std::string_view name, SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getChild(translateToPlatformPathId(id), name, symbolTable), symbolTable);
}

PathId AvfsFileSystem::getSibling(PathId id, std::string_view name, SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getSibling(translateToPlatformPathId(id), name, symbolTable),
                                     symbolTable);
}

PathId AvfsFileSystem::getParent(PathId id, SymbolTable* symbolTable) {
  return translateFromPlatformPathId(m_platform->getParent(translateToPlatformPathId(id), symbolTable), symbolTable);
}

std::pair<SymbolId, std::string_view> AvfsFileSystem::getLeaf(PathId id, SymbolTable* symbolTable) {
  return m_platform->getLeaf(translateToPlatformPathId(id), symbolTable);
}

std::pair<SymbolId, std::string_view> AvfsFileSystem::getType(PathId id, SymbolTable* symbolTable) {
  return m_platform->getType(translateToPlatformPathId(id), symbolTable);
}

PathId AvfsFileSystem::copy(PathId id, SymbolTable* toSymbolTable) { return FileSystem::copy(id, toSymbolTable); }

void AvfsFileSystem::printConfiguration(std::ostream& out) {
  m_platform->printConfiguration(out);
  out << "avfs mounts:" << std::endl;

  std::vector<MountRegistration> mounts;
  {
    std::scoped_lock<std::mutex> lock(m_mountsMutex);
    mounts = m_mounts;
  }

  for (const MountRegistration& mount : mounts) {
    out << "  " << mount.m_variablePrefix << " => " << mount.m_root.string() << std::endl;
  }
}

}  // namespace SURELOG
