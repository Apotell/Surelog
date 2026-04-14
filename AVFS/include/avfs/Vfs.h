#pragma once

#include <functional>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace avfs {

class IFileSystem {
 public:
  virtual ~IFileSystem() = default;

  virtual std::unique_ptr<std::istream> openRead(const std::string& path) const = 0;
  virtual std::unique_ptr<std::ostream> openWrite(const std::string& path) = 0;
  virtual bool exists(const std::string& path) const = 0;
};

class PlatformFileSystem final : public IFileSystem {
 public:
  explicit PlatformFileSystem(std::string rootDirectory);

  std::unique_ptr<std::istream> openRead(const std::string& path) const override;
  std::unique_ptr<std::ostream> openWrite(const std::string& path) override;
  bool exists(const std::string& path) const override;

 private:
  std::string rootDirectory_;
};

class InMemoryFileSystem final : public IFileSystem {
 public:
  InMemoryFileSystem() = default;

  std::unique_ptr<std::istream> openRead(const std::string& path) const override;
  std::unique_ptr<std::ostream> openWrite(const std::string& path) override;
  bool exists(const std::string& path) const override;

  void seedFile(std::string path, std::string contents);
  std::string readFile(const std::string& path) const;

 private:
  std::unordered_map<std::string, std::string> files_;
  mutable std::mutex mutex_;
};

class TarFileSystem final : public IFileSystem {
 public:
  explicit TarFileSystem(std::string archivePath);

  std::unique_ptr<std::istream> openRead(const std::string& path) const override;
  std::unique_ptr<std::ostream> openWrite(const std::string& path) override;
  bool exists(const std::string& path) const override;

 private:
  void ensureLoaded() const;

  std::string archivePath_;
  mutable bool loaded_ = false;
  mutable std::unordered_map<std::string, std::string> files_;
  mutable std::mutex mutex_;
};

class CompressedFileSystem final : public IFileSystem {
 public:
  explicit CompressedFileSystem(std::shared_ptr<IFileSystem> backingStore);

  std::unique_ptr<std::istream> openRead(const std::string& path) const override;
  std::unique_ptr<std::ostream> openWrite(const std::string& path) override;
  bool exists(const std::string& path) const override;

 private:
  std::shared_ptr<IFileSystem> backingStore_;
};

class EncryptedFileSystem final : public IFileSystem {
 public:
  EncryptedFileSystem(std::shared_ptr<IFileSystem> backingStore, std::string key);

  std::unique_ptr<std::istream> openRead(const std::string& path) const override;
  std::unique_ptr<std::ostream> openWrite(const std::string& path) override;
  bool exists(const std::string& path) const override;

 private:
  std::shared_ptr<IFileSystem> backingStore_;
  std::string key_;
};

class INetworkTransport {
 public:
  virtual ~INetworkTransport() = default;

  virtual std::string fetch(const std::string& uri) const = 0;
  virtual void store(const std::string& uri, const std::string& payload) = 0;
  virtual bool exists(const std::string& uri) const = 0;
};

class InMemoryNetworkTransport final : public INetworkTransport {
 public:
  std::string fetch(const std::string& uri) const override;
  void store(const std::string& uri, const std::string& payload) override;
  bool exists(const std::string& uri) const override;

  void seed(std::string uri, std::string payload);

 private:
  std::unordered_map<std::string, std::string> payloads_;
  mutable std::mutex mutex_;
};

class HttpNetworkTransport final : public INetworkTransport {
 public:
  std::string fetch(const std::string& uri) const override;
  void store(const std::string& uri, const std::string& payload) override;
  bool exists(const std::string& uri) const override;
};

class NetworkFileSystem final : public IFileSystem {
 public:
  NetworkFileSystem(std::string baseUri, std::shared_ptr<INetworkTransport> transport);

  std::unique_ptr<std::istream> openRead(const std::string& path) const override;
  std::unique_ptr<std::ostream> openWrite(const std::string& path) override;
  bool exists(const std::string& path) const override;

 private:
  std::string makeUri(const std::string& path) const;

  std::string baseUri_;
  std::shared_ptr<INetworkTransport> transport_;
};

struct BackendOptions {
  std::string root;
  std::unordered_map<std::string, std::string> properties;
};

class BackendRegistry final {
 public:
  using Factory = std::function<std::shared_ptr<IFileSystem>(const BackendOptions&)>;

  void registerBackend(std::string backendType, Factory factory);
  std::shared_ptr<IFileSystem> create(const std::string& backendType, const BackendOptions& options) const;

  static BackendRegistry withDefaults();

 private:
  std::unordered_map<std::string, Factory> factories_;
  std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
};

class VirtualFileSystem final {
 public:
  static std::string normalizeLogicalPath(const std::string& path);
  static std::string normalizeRelativePath(const std::string& path);

  void mount(std::string mountPoint, std::shared_ptr<IFileSystem> fileSystem);

  std::unique_ptr<std::istream> openRead(const std::string& logicalPath) const;
  std::unique_ptr<std::ostream> openWrite(const std::string& logicalPath) const;
  bool exists(const std::string& logicalPath) const;

 private:
  struct Mount {
    std::string mountPoint;
    std::shared_ptr<IFileSystem> fileSystem;
  };

  static bool isMountMatch(const std::string& logicalPath, const std::string& mountPoint);
  const Mount& resolveMount(const std::string& logicalPath) const;
  std::string resolveRelativePath(const std::string& logicalPath, const Mount& mount) const;

  std::vector<Mount> mounts_;
  std::shared_ptr<std::shared_mutex> mutex_ = std::make_shared<std::shared_mutex>();
};

class LegacyPathResolver final {
 public:
  void bind(std::string variableName, std::string logicalMountPoint);
  std::string expand(const std::string& path) const;

 private:
  std::unordered_map<std::string, std::string> bindings_;
  std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
};

class VfsRuntime final {
 public:
  void mount(std::string mountPoint, std::shared_ptr<IFileSystem> fileSystem);
  void mount(std::string mountPoint, const std::string& backendType, const BackendOptions& options);

  void mountVariable(std::string variableName, std::string mountPoint, std::shared_ptr<IFileSystem> fileSystem);
  void mountVariable(std::string variableName, std::string mountPoint, const std::string& backendType,
                     const BackendOptions& options);

  void bindVariable(std::string variableName, std::string logicalMountPoint);
  std::string expandPath(const std::string& path) const;

  std::unique_ptr<std::istream> openRead(const std::string& path) const;
  std::unique_ptr<std::ostream> openWrite(const std::string& path);
  bool exists(const std::string& path) const;

  BackendRegistry& backends() { return backends_; }
  const BackendRegistry& backends() const { return backends_; }

  VirtualFileSystem& vfs() { return vfs_; }
  const VirtualFileSystem& vfs() const { return vfs_; }

 private:
  BackendRegistry backends_ = BackendRegistry::withDefaults();
  VirtualFileSystem vfs_;
  LegacyPathResolver resolver_;
};

}  // namespace avfs
