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

#include <Surelog/Common/FileSystem.h>
#include <Surelog/Common/PlatformFileSystem.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace avfs {
class VfsRuntime;
}

namespace SURELOG {
class SymbolTable;

class AvfsFileSystem final : public FileSystem {
  // AVFS owns mount registration and managed file I/O.
  // PlatformFileSystem remains required to satisfy the wider legacy FileSystem
  // contract for host-path translation, working-directory handling, directory
  // traversal, and filesystem mutations outside the current VfsRuntime API.
 public:
  struct MountInfo final {
    // Bare AVFS variable name without the leading '$', e.g. "DataDir".
    // AvfsFileSystem derives the logical mount point as "/<m_variableName>".
    // HS: Why does this not include the "$" prefix. Paths are not /variable-name.
    // They should be $variable-name/. The virtual paths are not rooted with "/".
    // The "$variable-name" is the root.
    std::string m_variableName;
    // Backend registry key, e.g. "platform", "memory", or "tar".
    std::string m_backendType;
    // Backend root option passed to AVFS. For platform mounts this is the host root.
    std::string m_root;
    // Backend-specific properties forwarded to the AVFS backend factory.
    // HS: Assuming this is content loaded from m_configPath. If it is not then
    // what does this hold, and if it is then this should be std::map<std::string, std::any>
    // to keep it flexible.
    std::unordered_map<std::string, std::string> m_properties;
    // Source JSON config that registered this mount, if any.
    // HS: VFS shouldn't be responsible for loading this configuraiton.
    // With filesystem::path object here you are once again assuming that VFS
    // has access to such a local/platform/native filesystem. Instead Accept the
    // configuration as a dictionary/map. Use std::map<string, std::any> to make it
    // generic.
    std::filesystem::path m_configPath;
  };

  explicit AvfsFileSystem(const std::filesystem::path& workingDir);
  ~AvfsFileSystem() override;

  // HS: What's the difference between variableName & root. If root is filesystem::path then it
  // should be sent down to the appropriate backend type as part of the properties. Why a separate
  // arg? Ideally, there should be one registration path -
  // bool mount(std::string type, std::string_view name, cons std::map<std::string_view, std::any> &config);
  // As part of the implementation, you create an instance of the backend based on type and pass
  // both name and config to initialize.
  bool registerMount(std::string_view variableName, const std::filesystem::path& root);
  bool registerMount(std::string_view variableName, std::string_view backendType, std::string_view root,
                     const std::unordered_map<std::string, std::string>& properties = {},
                     const std::filesystem::path& configPath = {});
  std::vector<MountInfo> getMounts() const;

  PathId toPathId(std::string_view path, SymbolTable* symbolTable) override;
  std::string_view toPath(PathId id) override;
  bool canResolveToPlatformPath(PathId id) override;
  std::filesystem::path toPlatformAbsPath(PathId id) override;
  std::filesystem::path toPlatformRelPath(PathId id) override;
  std::pair<std::filesystem::path, std::filesystem::path> toSplitPlatformPath(PathId id) override;

  std::string getWorkingDir() override;
  std::set<std::string> getWorkingDirs() override;

  std::istream& openInput(PathId fileId, std::ios_base::openmode mode) override;
  bool close(std::istream& strm) override;

  std::ostream& openOutput(PathId fileId, std::ios_base::openmode mode) override;
  bool close(std::ostream& strm) override;

  bool saveContent(PathId fileId, const char* content, std::streamsize length, bool useTemp) override;

  bool addMapping(std::string_view what, std::string_view with) override;
  std::string remap(std::string_view what) override;
  bool addWorkingDirectoryCacheEntry(std::string_view prefix, std::string_view suffix) override;

  PathId getProgramFile(std::string_view hint, SymbolTable* symbolTable) override;
  PathId getWorkingDir(std::string_view dir, SymbolTable* symbolTable) override;
  PathId getOutputDir(std::string_view dir, SymbolTable* symbolTable) override;
  PathId getPrecompiledDir(PathId programId, SymbolTable* symbolTable) override;

  using FileSystem::getLogFile;
  PathId getLogFile(std::string_view filename, SymbolTable* symbolTable) override;
  using FileSystem::getCacheDir;
  PathId getCacheDir(std::string_view dirname, SymbolTable* symbolTable) override;
  using FileSystem::getCompileDir;
  PathId getCompileDir(SymbolTable* symbolTable) override;
  using FileSystem::getPpOutputFile;
  PathId getPpOutputFile(PathId sourceFileId, std::string_view libraryName, SymbolTable* symbolTable) override;
  using FileSystem::getPpCacheFile;
  PathId getPpCacheFile(PathId sourceFileId, std::string_view libraryName, bool isPrecompiled,
                        SymbolTable* symbolTable) override;
  using FileSystem::getParseCacheFile;
  PathId getParseCacheFile(PathId ppFileId, std::string_view libraryName, bool isPrecompiled,
                           SymbolTable* symbolTable) override;
  using FileSystem::getPythonCacheFile;
  PathId getPythonCacheFile(PathId sourceFileId, std::string_view libraryName, SymbolTable* symbolTable) override;
  using FileSystem::getPpMultiprocessingDir;
  PathId getPpMultiprocessingDir(SymbolTable* symbolTable) override;
  using FileSystem::getParserMultiprocessingDir;
  PathId getParserMultiprocessingDir(SymbolTable* symbolTable) override;
  PathId getChunkFile(PathId ppFileId, int32_t chunkIndex, SymbolTable* symbolTable) override;
  using FileSystem::getCheckerDir;
  PathId getCheckerDir(SymbolTable* symbolTable) override;
  PathId getCheckerFile(PathId uhdmFileId, SymbolTable* symbolTable) override;
  PathId getCheckerHtmlFile(PathId uhdmFileId, SymbolTable* symbolTable) override;
  PathId getCheckerHtmlFile(PathId uhdmFileId, int32_t index, SymbolTable* symbolTable) override;
  using FileSystem::getOutputUhdmFile;
  PathId getOutputUhdmFile(SymbolTable* symbolTable) override;

  bool rename(PathId whatId, PathId toId) override;
  bool remove(PathId fileId) override;
  bool mkdir(PathId dirId) override;
  bool rmdir(PathId dirId) override;
  bool mkdirs(PathId dirId) override;
  bool rmtree(PathId dirId) override;
  bool exists(PathId id) override;
  bool exists(PathId dirId, std::string_view descendant) override;
  bool isDirectory(PathId id) override;
  bool isRegularFile(PathId id) override;
  bool filesize(PathId fileId, std::streamsize* result) override;
  std::filesystem::file_time_type modtime(PathId fileId,
                                          std::filesystem::file_time_type defaultOnFail) override;

  PathId locate(std::string_view name, const PathIdVector& directories, SymbolTable* symbolTable) override;
  PathIdVector& collect(PathId dirId, SymbolTable* symbolTable, PathIdVector& container) override;
  PathIdVector& collect(PathId dirId, std::string_view extension, SymbolTable* symbolTable,
                        PathIdVector& container) override;
  PathIdVector& matching(PathId dirId, std::string_view pattern, SymbolTable* symbolTable,
                         PathIdVector& container) override;
  PathIdVector& matching(PathId dirId, const std::regex& pattern, SymbolTable* symbolTable,
                         PathIdVector& container) override;

  PathId getChild(PathId id, std::string_view name, SymbolTable* symbolTable) override;
  PathId getSibling(PathId id, std::string_view name, SymbolTable* symbolTable) override;
  PathId getParent(PathId id, SymbolTable* symbolTable) override;
  std::pair<SymbolId, std::string_view> getLeaf(PathId id, SymbolTable* symbolTable) override;
  std::pair<SymbolId, std::string_view> getType(PathId id, SymbolTable* symbolTable) override;
  PathId copy(PathId id, SymbolTable* toSymbolTable) override;

  void printConfiguration(std::ostream& out) override;

 private:
  using InputStreams = std::vector<std::unique_ptr<std::istream>>;
  using OutputStreams = std::vector<std::unique_ptr<std::ostream>>;

  static std::string normalizeVariablePath(std::string_view path);
  static std::string normalizeVariableName(std::string_view variableName);
  static bool isManagedPath(std::string_view path);

  std::filesystem::path resolveManagedPath(std::string_view path) const;
  PathId translateToPlatformPathId(PathId id) const;
  PathId translateFromPlatformPathId(PathId id, SymbolTable* symbolTable) const;
  std::optional<MountInfo> findMountByVariable(std::string_view variableName) const;
  std::optional<MountInfo> findMountForPlatformPath(const std::filesystem::path& path) const;
  std::string makeManagedPath(const MountInfo& mount, const std::filesystem::path& path) const;
  std::istream& openManagedInput(std::string_view filepath, std::ios_base::openmode mode);
  std::ostream& openManagedOutput(std::string_view filepath, std::ios_base::openmode mode);
  std::filesystem::path resolveInputPath(std::string_view path) const;
  std::filesystem::path getPlatformRoot(const MountInfo& mount) const;

  // AVFS still adapts legacy FileSystem APIs that require host-visible paths.
  // HS: What API are we talking about here? All available APIs should be
  // updated to use VFS. If either case, if absolutely needed, the platform
  // system should one of the registered backend system, and not an explicit
  // instance maintained by the VFS. If the configuraiton dictates a platform
  // specific filesystem, you don't want to end up with two instances of the
  // same filesystem (nothing wrong with having two platform specific filesystems
  // but this specific use-case seems to be wrong).
  std::unique_ptr<PlatformFileSystem> m_platform;
  std::unique_ptr<avfs::VfsRuntime> m_runtime;
  std::mutex m_inputStreamsMutex;
  std::mutex m_outputStreamsMutex;
  InputStreams m_inputStreams;
  OutputStreams m_outputStreams;
};
}  // namespace SURELOG

#endif  // SURELOG_AVFSFILESYSTEM_H
