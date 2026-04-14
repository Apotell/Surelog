#include "avfs/Discovery.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace avfs {
namespace {

std::string trim(const std::string& value) {
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

std::string uppercase(const std::string& value) {
  std::string result = value;
  for (char& ch : result) {
    if (ch >= 'a' && ch <= 'z') {
      ch = static_cast<char>(ch - 'a' + 'A');
    }
  }
  return result;
}

std::string buildEnvName(const std::string& prefix, const MountSpec& spec) {
  return uppercase(prefix + "_" + spec.variableName + "_ROOT");
}

std::unordered_map<std::string, std::string> loadConfigMap(const DiscoveryOptions& options) {
  std::filesystem::path current = std::filesystem::absolute(options.workingDirectory);

  while (true) {
    const auto candidate = current / options.configFileName;
    if (std::filesystem::exists(candidate)) {
      std::ifstream input(candidate);
      if (!input) {
        throw std::runtime_error("failed to open discovery config: " + candidate.string());
      }

      std::unordered_map<std::string, std::string> config;
      std::string line;
      while (std::getline(input, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
          line.erase(comment);
        }
        line = trim(line);
        if (line.empty()) {
          continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
          throw std::runtime_error("invalid discovery config line: " + line);
        }
        config[trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
      }
      return config;
    }

    if (current == current.root_path()) {
      return {};
    }
    current = current.parent_path();
  }
}

std::string findByConvention(const MountSpec& spec, const DiscoveryOptions& options) {
  const auto base = std::filesystem::absolute(options.workingDirectory);
  for (const auto& relative : spec.conventionalRelativePaths) {
    const auto candidate = base / relative;
    if (std::filesystem::exists(candidate)) {
      return candidate.string();
    }
    if (spec.backendType == "platform") {
      const auto parent = candidate.parent_path();
      if (!parent.empty() && std::filesystem::exists(parent)) {
        return candidate.string();
      }
    }
  }
  return {};
}

}  // namespace

VfsRuntime DiscoveryBootstrap::buildRuntime(const std::vector<MountSpec>& specs, const DiscoveryOptions& options) {
  VfsRuntime runtime;
  for (const auto& spec : specs) {
    const auto root = discoverRootForMount(spec, options);
    if (root.empty()) {
      if (spec.required) {
        throw std::runtime_error("failed to discover root for mount: " + spec.mountPoint);
      }
      continue;
    }
    runtime.mountVariable(spec.variableName, spec.mountPoint, spec.backendType, {.root = root});
  }
  return runtime;
}

std::string DiscoveryBootstrap::discoverRootForMount(const MountSpec& spec, const DiscoveryOptions& options) {
  const auto envName = buildEnvName(options.envPrefix, spec);
  if (const char* envValue = std::getenv(envName.c_str())) {
    if (*envValue != '\0') {
      return envValue;
    }
  }

  const auto config = loadConfigMap(options);
  if (const auto it = config.find(spec.variableName); it != config.end() && !it->second.empty()) {
    return it->second;
  }
  if (const auto it = config.find(spec.mountPoint); it != config.end() && !it->second.empty()) {
    return it->second;
  }

  return findByConvention(spec, options);
}

}  // namespace avfs
