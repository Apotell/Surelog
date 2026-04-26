#include "avfs/Vfs.h"
#include "avfs/Discovery.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace {

void assertTrue(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Fn>
void assertThrowsRuntime(Fn&& fn, const std::string& message) {
  bool threw = false;
  try {
    fn();
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assertTrue(threw, message);
}

class ScopedEnvVar final {
 public:
  ScopedEnvVar(std::string name, std::string value) : name_(std::move(name)) {
    const char* existing = std::getenv(name_.c_str());
    if (existing) {
      hadPrevious_ = true;
      previous_ = existing;
    }
    ::setenv(name_.c_str(), value.c_str(), 1);
  }

  ~ScopedEnvVar() {
    if (hadPrevious_) {
      ::setenv(name_.c_str(), previous_.c_str(), 1);
    } else {
      ::unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  std::string previous_;
  bool hadPrevious_ = false;
};

std::string readAll(std::istream& stream) {
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

class LoopbackHttpServer final {
 public:
  LoopbackHttpServer() {
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
      throw std::runtime_error("failed to create HTTP test server socket");
    }

    int opt = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
      ::close(listenFd_);
      throw std::runtime_error("failed to bind HTTP test server socket");
    }
    if (::listen(listenFd_, 16) != 0) {
      ::close(listenFd_);
      throw std::runtime_error("failed to listen on HTTP test server socket");
    }

    socklen_t addrLen = sizeof(addr);
    if (::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0) {
      ::close(listenFd_);
      throw std::runtime_error("failed to query HTTP test server port");
    }
    port_ = ntohs(addr.sin_port);
    thread_ = std::thread([this]() { run(); });
  }

  ~LoopbackHttpServer() {
    stop_ = true;
    if (listenFd_ >= 0) {
      ::shutdown(listenFd_, SHUT_RDWR);
      ::close(listenFd_);
      listenFd_ = -1;
    }
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  std::string baseUri() const {
    return "http://127.0.0.1:" + std::to_string(port_);
  }

  void seed(std::string path, std::string body) {
    std::lock_guard<std::mutex> lock(mutex_);
    payloads_[std::move(path)] = std::move(body);
  }

 private:
  void run() {
    while (!stop_) {
      sockaddr_in clientAddr{};
      socklen_t clientLen = sizeof(clientAddr);
      const int clientFd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
      if (clientFd < 0) {
        if (stop_) {
          return;
        }
        continue;
      }
      handleClient(clientFd);
      ::close(clientFd);
    }
  }

  void handleClient(int clientFd) {
    std::string request;
    std::array<char, 4096> buffer{};
    std::size_t contentLength = 0;

    while (request.find("\r\n\r\n") == std::string::npos) {
      const auto bytes = ::recv(clientFd, buffer.data(), buffer.size(), 0);
      if (bytes <= 0) {
        return;
      }
      request.append(buffer.data(), static_cast<std::size_t>(bytes));
    }

    const auto headerEnd = request.find("\r\n\r\n");
    const auto headerText = request.substr(0, headerEnd);
    std::istringstream headerStream(headerText);
    std::string requestLine;
    std::getline(headerStream, requestLine);
    if (!requestLine.empty() && requestLine.back() == '\r') {
      requestLine.pop_back();
    }

    std::string method;
    std::string target;
    std::string version;
    {
      std::istringstream lineStream(requestLine);
      lineStream >> method >> target >> version;
    }

    std::string line;
    while (std::getline(headerStream, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      const auto colon = line.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      if (line.substr(0, colon) == "Content-Length") {
        contentLength = static_cast<std::size_t>(std::stoul(line.substr(colon + 1)));
      }
    }

    std::string body = request.substr(headerEnd + 4);
    while (body.size() < contentLength) {
      const auto bytes = ::recv(clientFd, buffer.data(), buffer.size(), 0);
      if (bytes <= 0) {
        break;
      }
      body.append(buffer.data(), static_cast<std::size_t>(bytes));
    }

    std::string responseBody;
    int status = 200;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (method == "GET") {
        const auto it = payloads_.find(target);
        if (it == payloads_.end()) {
          status = 404;
        } else {
          responseBody = it->second;
        }
      } else if (method == "HEAD") {
        status = payloads_.count(target) > 0 ? 200 : 404;
      } else if (method == "PUT") {
        payloads_[target] = body;
        status = 200;
      } else {
        status = 405;
      }
    }

    std::ostringstream response;
    response << "HTTP/1.1 " << status << " OK\r\n";
    response << "Content-Length: " << responseBody.size() << "\r\n";
    response << "Connection: close\r\n\r\n";
    response << responseBody;
    const auto responseText = response.str();
    std::size_t offset = 0;
    while (offset < responseText.size()) {
      const auto bytes = ::send(clientFd, responseText.data() + offset, responseText.size() - offset, 0);
      if (bytes <= 0) {
        break;
      }
      offset += static_cast<std::size_t>(bytes);
    }
  }

  int listenFd_ = -1;
  unsigned short port_ = 0;
  std::atomic<bool> stop_{false};
  std::thread thread_;
  std::mutex mutex_;
  std::unordered_map<std::string, std::string> payloads_;
};

void writeTarHeader(std::ostream& out, const std::string& name, std::size_t size) {
  std::array<char, 512> header{};

  std::snprintf(header.data(), 100, "%s", name.c_str());
  std::snprintf(header.data() + 100, 8, "%07o", 0644);
  std::snprintf(header.data() + 108, 8, "%07o", 0);
  std::snprintf(header.data() + 116, 8, "%07o", 0);
  std::snprintf(header.data() + 124, 12, "%011o", static_cast<unsigned int>(size));
  std::snprintf(header.data() + 136, 12, "%011o", 0U);
  std::memset(header.data() + 148, ' ', 8);
  header[156] = '0';
  std::memcpy(header.data() + 257, "ustar", 5);
  std::memcpy(header.data() + 263, "00", 2);

  unsigned int checksum = 0;
  for (unsigned char byte : header) {
    checksum += byte;
  }
  std::snprintf(header.data() + 148, 8, "%06o", checksum);
  header[154] = '\0';
  header[155] = ' ';

  out.write(header.data(), static_cast<std::streamsize>(header.size()));
}

void writeTarFile(const std::filesystem::path& tarPath,
                  const std::vector<std::pair<std::string, std::string>>& entries) {
  std::ofstream out(tarPath, std::ios::binary);
  for (const auto& [name, contents] : entries) {
    writeTarHeader(out, name, contents.size());
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));

    const std::size_t padding = (512 - (contents.size() % 512)) % 512;
    std::array<char, 512> zeros{};
    out.write(zeros.data(), static_cast<std::streamsize>(padding));
  }

  std::array<char, 1024> endBlocks{};
  out.write(endBlocks.data(), static_cast<std::streamsize>(endBlocks.size()));
}

void testInMemoryRoundTrip() {
  avfs::VirtualFileSystem vfs;
  auto inputFs = std::make_shared<avfs::InMemoryFileSystem>();
  auto outputFs = std::make_shared<avfs::InMemoryFileSystem>();

  inputFs->seedFile("dut.sv", "module dut; endmodule\n");
  vfs.mount("/input", inputFs);
  vfs.mount("/output", outputFs);

  auto input = vfs.openRead("/input/dut.sv");
  assertTrue(readAll(*input) == "module dut; endmodule\n", "expected logical read from mounted in-memory input");

  {
    auto output = vfs.openWrite("/output/surelog.log");
    *output << "parse ok\n";
  }

  assertTrue(outputFs->exists("surelog.log"), "expected output file to be created in mounted in-memory backend");
  assertTrue(outputFs->readFile("surelog.log") == "parse ok\n", "expected logical write to land in mounted output backend");
}

void testLongestMountPrefixWins() {
  avfs::VirtualFileSystem vfs;
  auto genericInput = std::make_shared<avfs::InMemoryFileSystem>();
  auto nestedInput = std::make_shared<avfs::InMemoryFileSystem>();

  genericInput->seedFile("test/dut.sv", "generic\n");
  nestedInput->seedFile("dut.sv", "nested\n");

  vfs.mount("/input", genericInput);
  vfs.mount("/input/test", nestedInput);

  auto stream = vfs.openRead("/input/test/dut.sv");
  assertTrue(readAll(*stream) == "nested\n", "expected longest mount prefix to win");
}

void testExactMountMatchResolvesRootEntry() {
  avfs::VirtualFileSystem vfs;
  auto rootFs = std::make_shared<avfs::InMemoryFileSystem>();
  rootFs->seedFile("/", "mount root\n");
  vfs.mount("/input", rootFs);

  auto stream = vfs.openRead("/input");
  assertTrue(readAll(*stream) == "mount root\n", "expected exact mount path to resolve to backend root entry");
}

void testPlatformFileSystemRoundTrip() {
  const auto tempRoot = std::filesystem::temp_directory_path() / "avfs-platform-test";
  std::filesystem::remove_all(tempRoot);
  std::filesystem::create_directories(tempRoot / "tests" / "AaFirstTest");
  std::filesystem::create_directories(tempRoot / "out" / "regression" / "AaFirstTest");

  {
    std::ofstream dut(tempRoot / "tests" / "AaFirstTest" / "dut.sv", std::ios::binary);
    dut << "module platform_dut; endmodule\n";
  }

  avfs::VirtualFileSystem vfs;
  vfs.mount("/input", std::make_shared<avfs::PlatformFileSystem>((tempRoot / "tests" / "AaFirstTest").string()));
  vfs.mount("/output", std::make_shared<avfs::PlatformFileSystem>((tempRoot / "out" / "regression" / "AaFirstTest").string()));

  auto input = vfs.openRead("/input/dut.sv");
  assertTrue(readAll(*input) == "module platform_dut; endmodule\n", "expected platform-backed read through logical path");

  {
    auto output = vfs.openWrite("/output/surelog.log");
    *output << "platform ok\n";
  }

  std::ifstream outputFile(tempRoot / "out" / "regression" / "AaFirstTest" / "surelog.log", std::ios::binary);
  assertTrue(readAll(outputFile) == "platform ok\n", "expected platform-backed write through logical path");

  std::filesystem::remove_all(tempRoot);
}

void testMissingMountFails() {
  avfs::VirtualFileSystem vfs;
  assertThrowsRuntime([&]() { (void)vfs.exists("/output/surelog.log"); },
                      "expected unresolved logical path to fail");
}

void testLegacyVariableExpansion() {
  avfs::VfsRuntime runtime;
  auto inputFs = std::make_shared<avfs::InMemoryFileSystem>();
  auto outputFs = std::make_shared<avfs::InMemoryFileSystem>();

  inputFs->seedFile("dut.sv", "legacy\n");
  runtime.mountVariable("Input", "/input", inputFs);
  runtime.mountVariable("$Output", "/output", outputFs);

  auto input = runtime.openRead("$Input/dut.sv");
  assertTrue(readAll(*input) == "legacy\n", "expected $Input path to resolve through the mounted VFS");

  {
    auto output = runtime.openWrite("$Output/surelog.log");
    *output << "legacy write\n";
  }

  assertTrue(outputFs->readFile("surelog.log") == "legacy write\n",
             "expected $Output path to write through the mounted VFS");
  assertTrue(runtime.expandPath("$Input/dut.sv") == "/input/dut.sv",
             "expected legacy variable expansion to produce a logical path");
  assertTrue(runtime.expandPath("$Input") == "/input", "expected bare variable expansion to produce the mount point");
}

void testRuntimeListsMountMetadata() {
  avfs::VfsRuntime runtime;
  runtime.mountVariable("Input", "/input", "platform", {.root = "/tmp/runtime-input", .properties = {{"mode", "rw"}}},
                        "mounts.json");

  const auto mounts = runtime.listMounts();
  assertTrue(mounts.size() == 1, "expected runtime to retain one registered mount descriptor");
  assertTrue(mounts.front().variableName == "Input", "expected variable name to be preserved in mount metadata");
  assertTrue(mounts.front().mountPoint == "/input", "expected logical mount point to be preserved");
  assertTrue(mounts.front().backendType == "platform", "expected backend type to be preserved");
  assertTrue(mounts.front().options.root == "/tmp/runtime-input", "expected backend root to be preserved");
  assertTrue(mounts.front().options.properties.at("mode") == "rw", "expected backend properties to be preserved");
  assertTrue(mounts.front().configPath == "mounts.json", "expected config path to be preserved");
}

void testUnboundVariableFails() {
  avfs::VfsRuntime runtime;
  assertThrowsRuntime([&]() { (void)runtime.expandPath("$Input/dut.sv"); },
                      "expected unresolved legacy variable to fail");
}

void testBackendRegistryMounting() {
  avfs::VfsRuntime runtime;
  runtime.mount("/scratch", "memory", {});

  {
    auto output = runtime.openWrite("/scratch/output.log");
    *output << "registry\n";
  }

  auto input = runtime.openRead("/scratch/output.log");
  assertTrue(readAll(*input) == "registry\n", "expected runtime to mount a backend created by the registry");
}

void testCustomBackendRegistration() {
  avfs::VfsRuntime runtime;
  runtime.backends().registerBackend("memory-alias", [](const avfs::BackendOptions&) {
    return std::make_shared<avfs::InMemoryFileSystem>();
  });

  runtime.mount("/alias", "memory-alias", {});

  {
    auto output = runtime.openWrite("/alias/data.txt");
    *output << "custom\n";
  }

  auto input = runtime.openRead("/alias/data.txt");
  assertTrue(readAll(*input) == "custom\n", "expected custom-registered backend to be usable for mounting");
}

void testTarFileSystemRead() {
  const auto tempRoot = std::filesystem::temp_directory_path() / "avfs-tar-test";
  std::filesystem::remove_all(tempRoot);
  std::filesystem::create_directories(tempRoot);

  const auto tarPath = tempRoot / "fixtures.tar";
  writeTarFile(tarPath, {{"dut.sv", "module tar_dut; endmodule\n"}, {"logs/run.log", "tar ok\n"}});

  avfs::VfsRuntime runtime;
  runtime.mount("/archive", "tar", {.root = tarPath.string()});

  auto input = runtime.openRead("/archive/dut.sv");
  assertTrue(readAll(*input) == "module tar_dut; endmodule\n", "expected read from tar-backed VFS mount");
  assertTrue(runtime.exists("/archive/logs/run.log"), "expected tar backend to resolve nested entries");

  std::filesystem::remove_all(tempRoot);
}

void testTarFileSystemRejectsWrites() {
  const auto tempRoot = std::filesystem::temp_directory_path() / "avfs-tar-write-test";
  std::filesystem::remove_all(tempRoot);
  std::filesystem::create_directories(tempRoot);

  const auto tarPath = tempRoot / "fixtures.tar";
  writeTarFile(tarPath, {{"dut.sv", "module tar_dut; endmodule\n"}});

  avfs::TarFileSystem tarFs(tarPath.string());
  assertThrowsRuntime([&]() { (void)tarFs.openWrite("surelog.log"); }, "expected tar backend to be read-only");

  std::filesystem::remove_all(tempRoot);
}

void testMalformedTarHeaderFails() {
  const auto tempRoot = std::filesystem::temp_directory_path() / "avfs-tar-malformed-test";
  std::filesystem::remove_all(tempRoot);
  std::filesystem::create_directories(tempRoot);

  const auto tarPath = tempRoot / "malformed.tar";
  {
    std::ofstream out(tarPath, std::ios::binary);
    std::array<char, 512> header{};
    std::snprintf(header.data() + 124, 12, "%011o", 1U);
    header[156] = '0';
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    out.put('X');
  }

  avfs::TarFileSystem tarFs(tarPath.string());
  assertThrowsRuntime([&]() { (void)tarFs.exists("dut.sv"); }, "expected malformed tar headers to fail");

  std::filesystem::remove_all(tempRoot);
}

void testCompressedFileSystemRoundTrip() {
  auto backingStore = std::make_shared<avfs::InMemoryFileSystem>();
  avfs::CompressedFileSystem compressed(backingStore);

  {
    auto output = compressed.openWrite("dut.sv");
    *output << "compressed payload\n";
  }

  assertTrue(backingStore->exists("dut.sv"), "expected compressed backend to persist data in backing store");
  assertTrue(backingStore->readFile("dut.sv") != "compressed payload\n",
             "expected backing store contents to be compressed rather than plain text");

  auto input = compressed.openRead("dut.sv");
  assertTrue(readAll(*input) == "compressed payload\n", "expected compressed backend to round-trip file contents");
}

void testCompressedFileSystemRejectsTruncatedPayload() {
  auto backingStore = std::make_shared<avfs::InMemoryFileSystem>();
  backingStore->seedFile("dut.sv", std::string("\x1f\x8b\x08\x00", 4));
  avfs::CompressedFileSystem compressed(backingStore);

  assertThrowsRuntime([&]() {
    auto input = compressed.openRead("dut.sv");
    (void)readAll(*input);
  }, "expected compressed backend to reject truncated gzip payloads");
}

void testEncryptedFileSystemRoundTrip() {
  auto backingStore = std::make_shared<avfs::InMemoryFileSystem>();
  avfs::EncryptedFileSystem encrypted(backingStore, "secret");

  {
    auto output = encrypted.openWrite("dut.sv");
    *output << "sensitive payload\n";
  }

  assertTrue(backingStore->readFile("dut.sv") != "sensitive payload\n",
             "expected encrypted backend to hide plain text in backing store");

  auto input = encrypted.openRead("dut.sv");
  assertTrue(readAll(*input) == "sensitive payload\n", "expected encrypted backend to round-trip file contents");
}

void testEncryptedFileSystemRejectsTampering() {
  auto backingStore = std::make_shared<avfs::InMemoryFileSystem>();
  avfs::EncryptedFileSystem encrypted(backingStore, "secret");

  {
    auto output = encrypted.openWrite("dut.sv");
    *output << "tamper me\n";
  }

  auto stored = backingStore->readFile("dut.sv");
  assertTrue(stored.size() > 50, "expected encrypted payload to include header, nonce, tag, and ciphertext");
  stored.back() ^= 0x01;
  backingStore->seedFile("dut.sv", stored);

  assertThrowsRuntime([&]() {
    auto input = encrypted.openRead("dut.sv");
    (void)readAll(*input);
  }, "expected encrypted backend to reject tampered ciphertext");
}

void testEncryptedFileSystemRejectsWrongKey() {
  auto backingStore = std::make_shared<avfs::InMemoryFileSystem>();
  avfs::EncryptedFileSystem writer(backingStore, "secret");
  avfs::EncryptedFileSystem reader(backingStore, "wrong-secret");

  {
    auto output = writer.openWrite("dut.sv");
    *output << "classified\n";
  }

  assertThrowsRuntime([&]() {
    auto input = reader.openRead("dut.sv");
    (void)readAll(*input);
  }, "expected encrypted backend to reject decryption with the wrong key");
}

void testEncryptedFileSystemRejectsMalformedHeaderVariants() {
  auto backingStore = std::make_shared<avfs::InMemoryFileSystem>();
  avfs::EncryptedFileSystem encrypted(backingStore, "secret");

  backingStore->seedFile("bad-magic.bin", "BROKENHDRpayload");
  assertThrowsRuntime([&]() {
    auto input = encrypted.openRead("bad-magic.bin");
    (void)readAll(*input);
  }, "expected encrypted backend to reject invalid header magic");

  backingStore->seedFile("too-short.bin", "AVFSENC1");
  assertThrowsRuntime([&]() {
    auto input = encrypted.openRead("too-short.bin");
    (void)readAll(*input);
  }, "expected encrypted backend to reject truncated encrypted headers");
}

void testNetworkFileSystemRoundTrip() {
  auto transport = std::make_shared<avfs::InMemoryNetworkTransport>();
  transport->seed("https://example.test/input/dut.sv", "network payload\n");

  avfs::NetworkFileSystem network("https://example.test/input", transport);

  auto input = network.openRead("dut.sv");
  assertTrue(readAll(*input) == "network payload\n", "expected network backend to fetch via transport");

  {
    auto output = network.openWrite("surelog.log");
    *output << "network write\n";
  }

  assertTrue(transport->exists("https://example.test/input/surelog.log"),
             "expected network backend write to store payload through transport");
  assertTrue(transport->fetch("https://example.test/input/surelog.log") == "network write\n",
             "expected network backend to preserve written payload");
  assertTrue(network.exists("surelog.log"), "expected network backend exists() to delegate to the transport");
}

void testHttpNetworkTransportRoundTrip() {
  LoopbackHttpServer server;
  server.seed("/input/dut.sv", "http payload\n");

  auto transport = std::make_shared<avfs::HttpNetworkTransport>();
  avfs::NetworkFileSystem network(server.baseUri() + "/input", transport);

  auto input = network.openRead("dut.sv");
  assertTrue(readAll(*input) == "http payload\n", "expected HTTP transport to fetch from a real loopback server");

  {
    auto output = network.openWrite("surelog.log");
    *output << "http write\n";
  }

  assertTrue(network.exists("surelog.log"), "expected HTTP transport HEAD to reflect loopback server state");
  auto written = network.openRead("surelog.log");
  assertTrue(readAll(*written) == "http write\n", "expected HTTP transport PUT to persist data on the server");
}

void testConcurrentAccessThreadSafety() {
  avfs::VfsRuntime runtime;
  auto memoryFs = std::make_shared<avfs::InMemoryFileSystem>();
  runtime.mount("/scratch", memoryFs);

  std::vector<std::thread> writers;
  for (int index = 0; index < 8; ++index) {
    writers.emplace_back([index, &runtime]() {
      for (int iteration = 0; iteration < 100; ++iteration) {
        const auto path = "/scratch/file_" + std::to_string(index) + "_" + std::to_string(iteration) + ".txt";
        auto output = runtime.openWrite(path);
        *output << "payload-" << index << "-" << iteration;
      }
    });
  }
  for (auto& thread : writers) {
    thread.join();
  }

  std::vector<std::thread> readers;
  std::atomic<int> verified{0};
  for (int index = 0; index < 8; ++index) {
    readers.emplace_back([index, &runtime, &verified]() {
      for (int iteration = 0; iteration < 100; ++iteration) {
        const auto path = "/scratch/file_" + std::to_string(index) + "_" + std::to_string(iteration) + ".txt";
        auto input = runtime.openRead(path);
        const auto expected = "payload-" + std::to_string(index) + "-" + std::to_string(iteration);
        if (readAll(*input) == expected) {
          ++verified;
        }
      }
    });
  }
  for (auto& thread : readers) {
    thread.join();
  }

  assertTrue(verified == 800, "expected concurrent access to preserve all written file contents");
}

void testDiscoveryFromEnvironment() {
  const auto tempRoot = std::filesystem::temp_directory_path() / "avfs-discovery-env";
  std::filesystem::remove_all(tempRoot);
  std::filesystem::create_directories(tempRoot);

  {
    std::ofstream dut(tempRoot / "dut.sv");
    dut << "env discovery\n";
  }

  ScopedEnvVar inputEnv("AVFS_INPUT_ROOT", tempRoot.string());
  const avfs::MountSpec spec{"Input", "/input", "platform", true, {"input"}};
  const auto root = avfs::DiscoveryBootstrap::discoverRootForMount(spec, {});
  assertTrue(root == tempRoot.string(), "expected environment variable discovery to win");

  const auto runtime = avfs::DiscoveryBootstrap::buildRuntime({spec});
  auto input = runtime.openRead("$Input/dut.sv");
  assertTrue(readAll(*input) == "env discovery\n", "expected discovered environment mount to be usable");

  std::filesystem::remove_all(tempRoot);
}

void testDiscoveryFromParentConfig() {
  const auto tempRoot = std::filesystem::temp_directory_path() / "avfs-discovery-config";
  const auto projectRoot = tempRoot / "project";
  const auto nested = projectRoot / "subdir" / "inner";
  const auto inputRoot = tempRoot / "inputs";
  std::filesystem::remove_all(tempRoot);
  std::filesystem::create_directories(nested);
  std::filesystem::create_directories(inputRoot);

  {
    std::ofstream config(projectRoot / ".avfs.mounts");
    config << "Input = " << inputRoot.string() << "\n";
  }
  {
    std::ofstream dut(inputRoot / "dut.sv");
    dut << "config discovery\n";
  }

  const avfs::DiscoveryOptions options{nested.string(), ".avfs.mounts", "AVFS"};
  const avfs::MountSpec spec{"Input", "/input", "platform", true, {"input"}};
  const auto root = avfs::DiscoveryBootstrap::discoverRootForMount(spec, options);
  assertTrue(root == inputRoot.string(), "expected discovery to find config in a parent directory");

  const auto runtime = avfs::DiscoveryBootstrap::buildRuntime({spec}, options);
  auto input = runtime.openRead("$Input/dut.sv");
  assertTrue(readAll(*input) == "config discovery\n", "expected parent-config-discovered mount to be usable");

  std::filesystem::remove_all(tempRoot);
}

void testDiscoveryFromConvention() {
  const auto tempRoot = std::filesystem::temp_directory_path() / "avfs-discovery-convention";
  const auto inputRoot = tempRoot / "input";
  std::filesystem::remove_all(tempRoot);
  std::filesystem::create_directories(inputRoot);

  {
    std::ofstream dut(inputRoot / "dut.sv");
    dut << "convention discovery\n";
  }

  const avfs::DiscoveryOptions options{tempRoot.string(), ".avfs.mounts", "AVFS"};
  const avfs::MountSpec spec{"Input", "/input", "platform", true, {"input", "tests"}};
  const auto root = avfs::DiscoveryBootstrap::discoverRootForMount(spec, options);
  assertTrue(root == inputRoot.string(), "expected discovery to fall back to conventional directories");

  const auto runtime = avfs::DiscoveryBootstrap::buildRuntime({spec}, options);
  auto input = runtime.openRead("$Input/dut.sv");
  assertTrue(readAll(*input) == "convention discovery\n", "expected conventional discovery mount to be usable");

  std::filesystem::remove_all(tempRoot);
}

void testDiscoveryFromConventionAllowsMissingPlatformOutputRoot() {
  const auto tempRoot = std::filesystem::temp_directory_path() / "avfs-discovery-output-convention";
  std::filesystem::remove_all(tempRoot);
  std::filesystem::create_directories(tempRoot);

  const avfs::DiscoveryOptions options{tempRoot.string(), ".avfs.mounts", "AVFS"};
  const avfs::MountSpec outputSpec{"Output", "/output", "platform", true, {"output"}};
  const auto root = avfs::DiscoveryBootstrap::discoverRootForMount(outputSpec, options);
  assertTrue(root == (tempRoot / "output").string(),
             "expected platform convention discovery to allow a missing writable root");

  auto runtime = avfs::DiscoveryBootstrap::buildRuntime({outputSpec}, options);
  {
    auto output = runtime.openWrite("$Output/surelog.log");
    *output << "convention output\n";
  }

  std::ifstream input(tempRoot / "output" / "surelog.log");
  assertTrue(readAll(input) == "convention output\n",
             "expected platform output convention to create the root lazily on write");

  std::filesystem::remove_all(tempRoot);
}

void testRegistryEncryptedAndCompressedMounts() {
  const auto tempRoot = std::filesystem::temp_directory_path() / "avfs-registry-backed-test";
  std::filesystem::remove_all(tempRoot);
  std::filesystem::create_directories(tempRoot / "encrypted");
  std::filesystem::create_directories(tempRoot / "compressed");

  avfs::VfsRuntime runtime;
  runtime.mount("/enc", "encrypted", {.root = (tempRoot / "encrypted").string(), .properties = {{"key", "secret"}}});
  runtime.mount("/gz", "compressed", {.root = (tempRoot / "compressed").string()});

  {
    auto output = runtime.openWrite("/enc/secret.log");
    *output << "encrypted via registry\n";
  }
  {
    auto output = runtime.openWrite("/gz/data.log");
    *output << "compressed via registry\n";
  }

  auto encryptedInput = runtime.openRead("/enc/secret.log");
  auto compressedInput = runtime.openRead("/gz/data.log");

  assertTrue(readAll(*encryptedInput) == "encrypted via registry\n",
             "expected encrypted registry backend to round-trip contents");
  assertTrue(readAll(*compressedInput) == "compressed via registry\n",
             "expected compressed registry backend to round-trip contents");

  std::filesystem::remove_all(tempRoot);
}

}  // namespace

int main() {
  testInMemoryRoundTrip();
  testLongestMountPrefixWins();
  testExactMountMatchResolvesRootEntry();
  testPlatformFileSystemRoundTrip();
  testMissingMountFails();
  testLegacyVariableExpansion();
  testRuntimeListsMountMetadata();
  testUnboundVariableFails();
  testBackendRegistryMounting();
  testCustomBackendRegistration();
  testTarFileSystemRead();
  testTarFileSystemRejectsWrites();
  testMalformedTarHeaderFails();
  testCompressedFileSystemRoundTrip();
  testCompressedFileSystemRejectsTruncatedPayload();
  testEncryptedFileSystemRoundTrip();
  testEncryptedFileSystemRejectsTampering();
  testEncryptedFileSystemRejectsWrongKey();
  testEncryptedFileSystemRejectsMalformedHeaderVariants();
  testNetworkFileSystemRoundTrip();
  testHttpNetworkTransportRoundTrip();
  testConcurrentAccessThreadSafety();
  testRegistryEncryptedAndCompressedMounts();
  testDiscoveryFromEnvironment();
  testDiscoveryFromParentConfig();
  testDiscoveryFromConvention();
  testDiscoveryFromConventionAllowsMissingPlatformOutputRoot();
  std::cout << "All AVFS tests passed\n";
  return 0;
}
