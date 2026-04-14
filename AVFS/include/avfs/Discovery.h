#pragma once

#include "avfs/Vfs.h"

#include <string>
#include <vector>

namespace avfs {

struct MountSpec {
  std::string variableName;
  std::string mountPoint;
  std::string backendType = "platform";
  bool required = true;
  std::vector<std::string> conventionalRelativePaths;
};

struct DiscoveryOptions {
  std::string workingDirectory = ".";
  std::string configFileName = ".avfs.mounts";
  std::string envPrefix = "AVFS";
};

class DiscoveryBootstrap final {
 public:
  static VfsRuntime buildRuntime(const std::vector<MountSpec>& specs, const DiscoveryOptions& options = {});
  static std::string discoverRootForMount(const MountSpec& spec, const DiscoveryOptions& options);
};

}  // namespace avfs
