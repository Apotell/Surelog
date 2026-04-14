#include "avfs/Vfs.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <netdb.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <shared_mutex>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <utility>
#include <zlib.h>

namespace avfs {
namespace {

std::string normalizeSlashPath(const std::string& rawPath) {
  if (rawPath.empty()) {
    throw std::invalid_argument("path must not be empty");
  }

  std::string normalized;
  normalized.reserve(rawPath.size());

  bool previousWasSlash = false;
  for (char ch : rawPath) {
    const char current = (ch == '\\') ? '/' : ch;
    if (current == '/') {
      if (!previousWasSlash) {
        normalized.push_back(current);
      }
      previousWasSlash = true;
      continue;
    }
    previousWasSlash = false;
    normalized.push_back(current);
  }

  if (normalized.empty() || normalized.front() != '/') {
    normalized.insert(normalized.begin(), '/');
  }

  if (normalized.size() > 1 && normalized.back() == '/') {
    normalized.pop_back();
  }

  return normalized;
}

class MemoryOutputBuffer final : public std::stringbuf {
 public:
  explicit MemoryOutputBuffer(std::string* target) : target_(target) {}

  ~MemoryOutputBuffer() override {
    sync();
  }

  int sync() override {
    *target_ = str();
    return 0;
  }

 private:
  std::string* target_;
};

class MemoryOutputStream final : public std::ostream {
 public:
  explicit MemoryOutputStream(std::string* target)
      : std::ostream(nullptr), buffer_(target) {
    rdbuf(&buffer_);
  }

 private:
  MemoryOutputBuffer buffer_;
};

std::filesystem::path joinUnderRoot(const std::string& rootDirectory, const std::string& relativePath) {
  std::filesystem::path fullPath(rootDirectory);
  if (!relativePath.empty()) {
    fullPath /= std::filesystem::path(relativePath);
  }
  return fullPath.lexically_normal();
}

std::string normalizeVariableName(const std::string& variableName) {
  if (variableName.empty()) {
    throw std::invalid_argument("variable name must not be empty");
  }

  if (variableName.front() == '$') {
    return variableName.substr(1);
  }

  return variableName;
}

std::pair<std::string, std::string> splitVariableReference(const std::string& path) {
  if (path.empty() || path.front() != '$') {
    return {"", path};
  }

  std::size_t separator = 1;
  while (separator < path.size()) {
    const char current = path[separator];
    if (current == '/' || current == '\\') {
      break;
    }
    ++separator;
  }

  return {path.substr(1, separator - 1), path.substr(separator)};
}

std::string readAll(std::istream& stream) {
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

struct ParsedHttpUri {
  std::string host;
  std::string port;
  std::string target;
};

ParsedHttpUri parseHttpUri(const std::string& uri) {
  constexpr const char* kHttpPrefix = "http://";
  constexpr std::size_t kHttpPrefixLength = 7;
  if (uri.rfind(kHttpPrefix, 0) != 0) {
    throw std::runtime_error("only http:// URIs are supported: " + uri);
  }

  const auto authorityStart = kHttpPrefixLength;
  const auto pathStart = uri.find('/', authorityStart);
  const auto authority = pathStart == std::string::npos ? uri.substr(authorityStart)
                                                        : uri.substr(authorityStart, pathStart - authorityStart);
  if (authority.empty()) {
    throw std::runtime_error("HTTP URI is missing an authority: " + uri);
  }

  const auto colon = authority.rfind(':');
  ParsedHttpUri parsed;
  parsed.host = colon == std::string::npos ? authority : authority.substr(0, colon);
  parsed.port = colon == std::string::npos ? "80" : authority.substr(colon + 1);
  parsed.target = pathStart == std::string::npos ? "/" : uri.substr(pathStart);
  if (parsed.host.empty() || parsed.port.empty()) {
    throw std::runtime_error("HTTP URI authority is invalid: " + uri);
  }
  return parsed;
}

class SocketHandle final {
 public:
  explicit SocketHandle(int fd = -1) : fd_(fd) {}

  ~SocketHandle() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  SocketHandle(const SocketHandle&) = delete;
  SocketHandle& operator=(const SocketHandle&) = delete;

  SocketHandle(SocketHandle&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
  }

  SocketHandle& operator=(SocketHandle&& other) noexcept {
    if (this != &other) {
      if (fd_ >= 0) {
        ::close(fd_);
      }
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  int get() const { return fd_; }

 private:
  int fd_;
};

void sendAllToSocket(int fd, const std::string& data) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const auto bytesSent = ::send(fd, data.data() + offset, data.size() - offset, 0);
    if (bytesSent <= 0) {
      throw std::runtime_error("socket send failed");
    }
    offset += static_cast<std::size_t>(bytesSent);
  }
}

std::string receiveAllFromSocket(int fd) {
  std::string response;
  std::array<char, 4096> buffer{};
  while (true) {
    const auto bytesRead = ::recv(fd, buffer.data(), buffer.size(), 0);
    if (bytesRead < 0) {
      throw std::runtime_error("socket receive failed");
    }
    if (bytesRead == 0) {
      break;
    }
    response.append(buffer.data(), static_cast<std::size_t>(bytesRead));
  }
  return response;
}

struct HttpResponse {
  int statusCode;
  std::string body;
};

HttpResponse performHttpRequest(const std::string& method, const std::string& uri, const std::string& body = {}) {
  const auto parsed = parseHttpUri(uri);

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* results = nullptr;
  if (::getaddrinfo(parsed.host.c_str(), parsed.port.c_str(), &hints, &results) != 0) {
    throw std::runtime_error("failed to resolve HTTP host: " + parsed.host);
  }

  SocketHandle socket;
  for (addrinfo* current = results; current != nullptr; current = current->ai_next) {
    SocketHandle candidate(::socket(current->ai_family, current->ai_socktype, current->ai_protocol));
    if (candidate.get() < 0) {
      continue;
    }
    if (::connect(candidate.get(), current->ai_addr, current->ai_addrlen) == 0) {
      socket = std::move(candidate);
      break;
    }
  }
  ::freeaddrinfo(results);

  if (socket.get() < 0) {
    throw std::runtime_error("failed to connect to HTTP host: " + parsed.host);
  }

  std::ostringstream request;
  request << method << " " << parsed.target << " HTTP/1.1\r\n";
  request << "Host: " << parsed.host << "\r\n";
  request << "Connection: close\r\n";
  if (method == "PUT") {
    request << "Content-Length: " << body.size() << "\r\n";
  }
  request << "\r\n";
  request << body;

  sendAllToSocket(socket.get(), request.str());
  const auto responseText = receiveAllFromSocket(socket.get());
  const auto headerEnd = responseText.find("\r\n\r\n");
  if (headerEnd == std::string::npos) {
    throw std::runtime_error("malformed HTTP response");
  }

  std::istringstream statusStream(responseText.substr(0, headerEnd));
  std::string statusLine;
  std::getline(statusStream, statusLine);
  if (!statusLine.empty() && statusLine.back() == '\r') {
    statusLine.pop_back();
  }

  std::istringstream parser(statusLine);
  std::string version;
  int statusCode = 0;
  parser >> version >> statusCode;
  if (version.empty() || statusCode == 0) {
    throw std::runtime_error("malformed HTTP status line");
  }

  return HttpResponse{statusCode, responseText.substr(headerEnd + 4)};
}

std::string gzipCompress(const std::string& input) {
  z_stream stream{};
  if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
    throw std::runtime_error("failed to initialize gzip compressor");
  }

  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());

  std::string output;
  std::array<char, 4096> buffer{};

  int status = Z_OK;
  do {
    stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
    stream.avail_out = static_cast<uInt>(buffer.size());

    status = deflate(&stream, stream.avail_in == 0 ? Z_FINISH : Z_NO_FLUSH);
    if (status != Z_OK && status != Z_STREAM_END) {
      deflateEnd(&stream);
      throw std::runtime_error("gzip compression failed");
    }

    output.append(buffer.data(), buffer.size() - stream.avail_out);
  } while (status != Z_STREAM_END);

  deflateEnd(&stream);
  return output;
}

std::string gzipDecompress(const std::string& input) {
  z_stream stream{};
  if (inflateInit2(&stream, 15 + 32) != Z_OK) {
    throw std::runtime_error("failed to initialize gzip decompressor");
  }

  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());

  std::string output;
  std::array<char, 4096> buffer{};

  int status = Z_OK;
  do {
    stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
    stream.avail_out = static_cast<uInt>(buffer.size());

    status = inflate(&stream, Z_NO_FLUSH);
    if (status != Z_OK && status != Z_STREAM_END) {
      inflateEnd(&stream);
      throw std::runtime_error("gzip decompression failed");
    }

    output.append(buffer.data(), buffer.size() - stream.avail_out);
  } while (status != Z_STREAM_END);

  if (status != Z_STREAM_END) {
    inflateEnd(&stream);
    throw std::runtime_error("gzip payload is truncated");
  }

  inflateEnd(&stream);
  return output;
}

constexpr std::size_t kSha256Length = SHA256_DIGEST_LENGTH;
constexpr std::size_t kEncryptionNonceLength = 32;
constexpr char kEncryptedMagic[] = "AVFSENC1";

std::string sha256Digest(const std::string& input) {
  std::array<unsigned char, kSha256Length> digest{};
  SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest.data());
  return std::string(reinterpret_cast<const char*>(digest.data()), digest.size());
}

std::string randomBytes(std::size_t size) {
  std::string output(size, '\0');
  if (size == 0) {
    return output;
  }
  if (RAND_bytes(reinterpret_cast<unsigned char*>(output.data()), static_cast<int>(output.size())) != 1) {
    throw std::runtime_error("failed to generate cryptographic randomness");
  }
  return output;
}

std::string deriveKeystreamBlock(const std::string& keyMaterial, const std::string& nonce, std::uint64_t counter) {
  std::string blockInput;
  blockInput.reserve(keyMaterial.size() + nonce.size() + sizeof(counter));
  blockInput.append(keyMaterial);
  blockInput.append(nonce);
  for (int shift = 56; shift >= 0; shift -= 8) {
    blockInput.push_back(static_cast<char>((counter >> shift) & 0xffU));
  }
  return sha256Digest(blockInput);
}

std::string applySha256StreamCipher(const std::string& input, const std::string& keyMaterial,
                                    const std::string& nonce) {
  std::string output = input;
  std::uint64_t counter = 0;

  for (std::size_t offset = 0; offset < output.size(); offset += kSha256Length, ++counter) {
    const auto keystream = deriveKeystreamBlock(keyMaterial, nonce, counter);
    const auto chunkSize = std::min<std::size_t>(kSha256Length, output.size() - offset);
    for (std::size_t index = 0; index < chunkSize; ++index) {
      output[offset + index] ^= keystream[index];
    }
  }

  return output;
}

std::string computeIntegrityTag(const std::string& keyMaterial, const std::string& nonce,
                                const std::string& cipherText) {
  std::string tagInput;
  tagInput.reserve(keyMaterial.size() + nonce.size() + cipherText.size() + 3);
  tagInput.append(keyMaterial);
  tagInput.append("TAG");
  tagInput.append(nonce);
  tagInput.append(cipherText);
  return sha256Digest(tagInput);
}

bool constantTimeEquals(const std::string& lhs, const std::string& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  unsigned char diff = 0;
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    diff |= static_cast<unsigned char>(lhs[index] ^ rhs[index]);
  }
  return diff == 0;
}

std::string encodeEncryptedPayload(const std::string& plainText, const std::string& key) {
  const auto keyMaterial = sha256Digest(key);
  const auto nonce = randomBytes(kEncryptionNonceLength);
  const auto cipherText = applySha256StreamCipher(plainText, keyMaterial, nonce);
  const auto tag = computeIntegrityTag(keyMaterial, nonce, cipherText);

  std::string payload;
  payload.reserve(sizeof(kEncryptedMagic) - 1 + nonce.size() + tag.size() + cipherText.size());
  payload.append(kEncryptedMagic, sizeof(kEncryptedMagic) - 1);
  payload.append(nonce);
  payload.append(tag);
  payload.append(cipherText);
  return payload;
}

std::string decodeEncryptedPayload(const std::string& payload, const std::string& key) {
  const std::size_t prefixSize = sizeof(kEncryptedMagic) - 1;
  const std::size_t minimumSize = prefixSize + kEncryptionNonceLength + kSha256Length;
  if (payload.size() < minimumSize) {
    throw std::runtime_error("encrypted payload is truncated");
  }
  if (payload.compare(0, prefixSize, kEncryptedMagic, prefixSize) != 0) {
    throw std::runtime_error("encrypted payload has an invalid header");
  }

  const auto nonce = payload.substr(prefixSize, kEncryptionNonceLength);
  const auto storedTag = payload.substr(prefixSize + kEncryptionNonceLength, kSha256Length);
  const auto cipherText = payload.substr(prefixSize + kEncryptionNonceLength + kSha256Length);

  const auto keyMaterial = sha256Digest(key);
  const auto expectedTag = computeIntegrityTag(keyMaterial, nonce, cipherText);
  if (!constantTimeEquals(storedTag, expectedTag)) {
    throw std::runtime_error("encrypted payload failed integrity verification");
  }

  return applySha256StreamCipher(cipherText, keyMaterial, nonce);
}

std::string joinUri(const std::string& baseUri, const std::string& relativePath) {
  std::string uri = baseUri;
  if (!uri.empty() && uri.back() == '/') {
    uri.pop_back();
  }

  if (relativePath.empty()) {
    return uri;
  }

  return uri + "/" + VirtualFileSystem::normalizeRelativePath(relativePath);
}

std::size_t parseTarOctal(const char* field, std::size_t fieldSize) {
  std::size_t value = 0;
  for (std::size_t index = 0; index < fieldSize; ++index) {
    const char current = field[index];
    if (current == '\0' || current == ' ') {
      continue;
    }
    if (current < '0' || current > '7') {
      break;
    }
    value = (value * 8) + static_cast<std::size_t>(current - '0');
  }
  return value;
}

bool tarBlockIsZero(const std::array<char, 512>& block) {
  return std::all_of(block.begin(), block.end(), [](char byte) { return byte == '\0'; });
}

template <typename FlushFn>
class BufferingOutputStream final : public std::ostream {
 public:
  explicit BufferingOutputStream(FlushFn flush)
      : std::ostream(nullptr), buffer_(std::move(flush)) {
    rdbuf(&buffer_);
  }

 private:
  class Buffer final : public std::stringbuf {
   public:
    explicit Buffer(FlushFn flush) : flush_(std::move(flush)) {}

    ~Buffer() override {
      sync();
    }

    int sync() override {
      flush_(str());
      return 0;
    }

   private:
    FlushFn flush_;
  };

  Buffer buffer_;
};

}  // namespace

PlatformFileSystem::PlatformFileSystem(std::string rootDirectory)
    : rootDirectory_(std::move(rootDirectory)) {}

std::unique_ptr<std::istream> PlatformFileSystem::openRead(const std::string& path) const {
  auto stream = std::make_unique<std::ifstream>(
      joinUnderRoot(rootDirectory_, VirtualFileSystem::normalizeRelativePath(path)), std::ios::binary);
  if (!*stream) {
    throw std::runtime_error("failed to open file for reading: " + path);
  }
  return stream;
}

std::unique_ptr<std::ostream> PlatformFileSystem::openWrite(const std::string& path) {
  const auto fullPath = joinUnderRoot(rootDirectory_, VirtualFileSystem::normalizeRelativePath(path));
  std::filesystem::create_directories(fullPath.parent_path());

  auto stream = std::make_unique<std::ofstream>(fullPath, std::ios::binary);
  if (!*stream) {
    throw std::runtime_error("failed to open file for writing: " + path);
  }
  return stream;
}

bool PlatformFileSystem::exists(const std::string& path) const {
  return std::filesystem::exists(joinUnderRoot(rootDirectory_, VirtualFileSystem::normalizeRelativePath(path)));
}

std::unique_ptr<std::istream> InMemoryFileSystem::openRead(const std::string& path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto normalized = VirtualFileSystem::normalizeRelativePath(path);
  const auto it = files_.find(normalized);
  if (it == files_.end()) {
    throw std::runtime_error("file does not exist in memory: " + normalized);
  }
  return std::make_unique<std::istringstream>(it->second);
}

std::unique_ptr<std::ostream> InMemoryFileSystem::openWrite(const std::string& path) {
  const auto normalized = VirtualFileSystem::normalizeRelativePath(path);
  return std::make_unique<BufferingOutputStream<std::function<void(const std::string&)>>>(
      [this, normalized](const std::string& contents) {
        std::lock_guard<std::mutex> lock(mutex_);
        files_[normalized] = contents;
      });
}

bool InMemoryFileSystem::exists(const std::string& path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return files_.count(VirtualFileSystem::normalizeRelativePath(path)) > 0;
}

void InMemoryFileSystem::seedFile(std::string path, std::string contents) {
  std::lock_guard<std::mutex> lock(mutex_);
  files_[VirtualFileSystem::normalizeRelativePath(path)] = std::move(contents);
}

std::string InMemoryFileSystem::readFile(const std::string& path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto normalized = VirtualFileSystem::normalizeRelativePath(path);
  const auto it = files_.find(normalized);
  if (it == files_.end()) {
    throw std::runtime_error("file does not exist in memory: " + normalized);
  }
  return it->second;
}

TarFileSystem::TarFileSystem(std::string archivePath)
    : archivePath_(std::move(archivePath)) {}

std::unique_ptr<std::istream> TarFileSystem::openRead(const std::string& path) const {
  ensureLoaded();
  const auto normalized = VirtualFileSystem::normalizeRelativePath(path);
  const auto it = files_.find(normalized);
  if (it == files_.end()) {
    throw std::runtime_error("file does not exist in tar archive: " + normalized);
  }
  return std::make_unique<std::istringstream>(it->second);
}

std::unique_ptr<std::ostream> TarFileSystem::openWrite(const std::string&) {
  throw std::runtime_error("TarFileSystem is read-only");
}

bool TarFileSystem::exists(const std::string& path) const {
  ensureLoaded();
  return files_.count(VirtualFileSystem::normalizeRelativePath(path)) > 0;
}

void TarFileSystem::ensureLoaded() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (loaded_) {
    return;
  }

  std::ifstream archive(archivePath_, std::ios::binary);
  if (!archive) {
    throw std::runtime_error("failed to open tar archive: " + archivePath_);
  }

  std::array<char, 512> header{};
  while (archive.read(header.data(), header.size())) {
    if (tarBlockIsZero(header)) {
      loaded_ = true;
      return;
    }

    const auto nameEnd = std::find(header.begin(), header.begin() + 100, '\0');
    const std::string name(header.data(), static_cast<std::size_t>(std::distance(header.begin(), nameEnd)));
    const char typeFlag = header[156];
    const std::size_t fileSize = parseTarOctal(header.data() + 124, 12);
    if (name.empty()) {
      throw std::runtime_error("tar entry is missing a name");
    }

    std::string contents(fileSize, '\0');
    if (fileSize > 0) {
      archive.read(contents.data(), static_cast<std::streamsize>(fileSize));
      if (!archive) {
        throw std::runtime_error("failed to read tar entry payload: " + name);
      }
    }

    if (typeFlag == '\0' || typeFlag == '0') {
      files_[VirtualFileSystem::normalizeRelativePath(name)] = std::move(contents);
    }

    const std::size_t padding = (512 - (fileSize % 512)) % 512;
    archive.ignore(static_cast<std::streamsize>(padding));
    if (!archive) {
      throw std::runtime_error("tar archive is truncated after entry: " + name);
    }
  }

  loaded_ = true;
}

CompressedFileSystem::CompressedFileSystem(std::shared_ptr<IFileSystem> backingStore)
    : backingStore_(std::move(backingStore)) {
  if (!backingStore_) {
    throw std::invalid_argument("CompressedFileSystem requires a backing store");
  }
}

std::unique_ptr<std::istream> CompressedFileSystem::openRead(const std::string& path) const {
  auto stream = backingStore_->openRead(path);
  return std::make_unique<std::istringstream>(gzipDecompress(readAll(*stream)));
}

std::unique_ptr<std::ostream> CompressedFileSystem::openWrite(const std::string& path) {
  return std::make_unique<BufferingOutputStream<std::function<void(const std::string&)>>>(
      [backingStore = backingStore_, normalizedPath = std::string(path)](const std::string& plainText) {
        auto stream = backingStore->openWrite(normalizedPath);
        const auto compressed = gzipCompress(plainText);
        stream->write(compressed.data(), static_cast<std::streamsize>(compressed.size()));
      });
}

bool CompressedFileSystem::exists(const std::string& path) const {
  return backingStore_->exists(path);
}

EncryptedFileSystem::EncryptedFileSystem(std::shared_ptr<IFileSystem> backingStore, std::string key)
    : backingStore_(std::move(backingStore)), key_(std::move(key)) {
  if (!backingStore_) {
    throw std::invalid_argument("EncryptedFileSystem requires a backing store");
  }
  if (key_.empty()) {
    throw std::invalid_argument("EncryptedFileSystem requires a non-empty key");
  }
}

std::unique_ptr<std::istream> EncryptedFileSystem::openRead(const std::string& path) const {
  auto stream = backingStore_->openRead(path);
  return std::make_unique<std::istringstream>(decodeEncryptedPayload(readAll(*stream), key_));
}

std::unique_ptr<std::ostream> EncryptedFileSystem::openWrite(const std::string& path) {
  return std::make_unique<BufferingOutputStream<std::function<void(const std::string&)>>>(
      [backingStore = backingStore_, key = key_, normalizedPath = std::string(path)](const std::string& plainText) {
        auto stream = backingStore->openWrite(normalizedPath);
        const auto encrypted = encodeEncryptedPayload(plainText, key);
        stream->write(encrypted.data(), static_cast<std::streamsize>(encrypted.size()));
      });
}

bool EncryptedFileSystem::exists(const std::string& path) const {
  return backingStore_->exists(path);
}

std::string InMemoryNetworkTransport::fetch(const std::string& uri) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = payloads_.find(uri);
  if (it == payloads_.end()) {
    throw std::runtime_error("network resource does not exist: " + uri);
  }
  return it->second;
}

void InMemoryNetworkTransport::store(const std::string& uri, const std::string& payload) {
  std::lock_guard<std::mutex> lock(mutex_);
  payloads_[uri] = payload;
}

bool InMemoryNetworkTransport::exists(const std::string& uri) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return payloads_.count(uri) > 0;
}

void InMemoryNetworkTransport::seed(std::string uri, std::string payload) {
  std::lock_guard<std::mutex> lock(mutex_);
  payloads_[std::move(uri)] = std::move(payload);
}

std::string HttpNetworkTransport::fetch(const std::string& uri) const {
  const auto response = performHttpRequest("GET", uri);
  if (response.statusCode != 200) {
    throw std::runtime_error("HTTP GET failed for " + uri);
  }
  return response.body;
}

void HttpNetworkTransport::store(const std::string& uri, const std::string& payload) {
  const auto response = performHttpRequest("PUT", uri, payload);
  if (response.statusCode != 200 && response.statusCode != 201 && response.statusCode != 204) {
    throw std::runtime_error("HTTP PUT failed for " + uri);
  }
}

bool HttpNetworkTransport::exists(const std::string& uri) const {
  const auto response = performHttpRequest("HEAD", uri);
  if (response.statusCode == 200) {
    return true;
  }
  if (response.statusCode == 404) {
    return false;
  }
  throw std::runtime_error("HTTP HEAD failed for " + uri);
}

NetworkFileSystem::NetworkFileSystem(std::string baseUri, std::shared_ptr<INetworkTransport> transport)
    : baseUri_(std::move(baseUri)), transport_(std::move(transport)) {
  if (!transport_) {
    throw std::invalid_argument("NetworkFileSystem requires a transport");
  }
}

std::unique_ptr<std::istream> NetworkFileSystem::openRead(const std::string& path) const {
  return std::make_unique<std::istringstream>(transport_->fetch(makeUri(path)));
}

std::unique_ptr<std::ostream> NetworkFileSystem::openWrite(const std::string& path) {
  return std::make_unique<BufferingOutputStream<std::function<void(const std::string&)>>>(
      [transport = transport_, uri = makeUri(path)](const std::string& payload) {
        transport->store(uri, payload);
      });
}

bool NetworkFileSystem::exists(const std::string& path) const {
  return transport_->exists(makeUri(path));
}

std::string NetworkFileSystem::makeUri(const std::string& path) const {
  return joinUri(baseUri_, path);
}

void BackendRegistry::registerBackend(std::string backendType, Factory factory) {
  std::lock_guard<std::mutex> lock(*mutex_);
  if (backendType.empty()) {
    throw std::invalid_argument("backend type must not be empty");
  }
  if (!factory) {
    throw std::invalid_argument("backend factory must not be empty");
  }

  factories_[std::move(backendType)] = std::move(factory);
}

std::shared_ptr<IFileSystem> BackendRegistry::create(const std::string& backendType,
                                                     const BackendOptions& options) const {
  std::lock_guard<std::mutex> lock(*mutex_);
  const auto it = factories_.find(backendType);
  if (it == factories_.end()) {
    throw std::runtime_error("backend type is not registered: " + backendType);
  }
  return it->second(options);
}

BackendRegistry BackendRegistry::withDefaults() {
  BackendRegistry registry;
  registry.registerBackend("platform", [](const BackendOptions& options) {
    return std::make_shared<PlatformFileSystem>(options.root);
  });
  registry.registerBackend("memory", [](const BackendOptions&) {
    return std::make_shared<InMemoryFileSystem>();
  });
  registry.registerBackend("tar", [](const BackendOptions& options) {
    return std::make_shared<TarFileSystem>(options.root);
  });
  registry.registerBackend("compressed", [](const BackendOptions& options) {
    return std::make_shared<CompressedFileSystem>(std::make_shared<PlatformFileSystem>(options.root));
  });
  registry.registerBackend("encrypted", [](const BackendOptions& options) {
    const auto keyIt = options.properties.find("key");
    if (keyIt == options.properties.end()) {
      throw std::runtime_error("encrypted backend requires a 'key' property");
    }
    return std::make_shared<EncryptedFileSystem>(
        std::make_shared<PlatformFileSystem>(options.root), keyIt->second);
  });
  return registry;
}

void VirtualFileSystem::mount(std::string mountPoint, std::shared_ptr<IFileSystem> fileSystem) {
  std::unique_lock<std::shared_mutex> lock(*mutex_);
  if (!fileSystem) {
    throw std::invalid_argument("mount requires a valid file system");
  }

  mounts_.push_back(Mount{normalizeLogicalPath(mountPoint), std::move(fileSystem)});
  std::sort(mounts_.begin(), mounts_.end(), [](const Mount& lhs, const Mount& rhs) {
    if (lhs.mountPoint.size() != rhs.mountPoint.size()) {
      return lhs.mountPoint.size() > rhs.mountPoint.size();
    }
    return lhs.mountPoint < rhs.mountPoint;
  });
}

std::unique_ptr<std::istream> VirtualFileSystem::openRead(const std::string& logicalPath) const {
  std::shared_lock<std::shared_mutex> lock(*mutex_);
  const auto normalized = normalizeLogicalPath(logicalPath);
  const auto& mount = resolveMount(normalized);
  return mount.fileSystem->openRead(resolveRelativePath(normalized, mount));
}

std::unique_ptr<std::ostream> VirtualFileSystem::openWrite(const std::string& logicalPath) const {
  std::shared_lock<std::shared_mutex> lock(*mutex_);
  const auto normalized = normalizeLogicalPath(logicalPath);
  const auto& mount = resolveMount(normalized);
  return mount.fileSystem->openWrite(resolveRelativePath(normalized, mount));
}

bool VirtualFileSystem::exists(const std::string& logicalPath) const {
  std::shared_lock<std::shared_mutex> lock(*mutex_);
  const auto normalized = normalizeLogicalPath(logicalPath);
  const auto& mount = resolveMount(normalized);
  return mount.fileSystem->exists(resolveRelativePath(normalized, mount));
}

std::string VirtualFileSystem::normalizeLogicalPath(const std::string& path) {
  return normalizeSlashPath(path);
}

std::string VirtualFileSystem::normalizeRelativePath(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  std::string normalized = normalizeSlashPath(path);
  if (normalized == "/") {
    return {};
  }
  normalized.erase(normalized.begin());
  return normalized;
}

bool VirtualFileSystem::isMountMatch(const std::string& logicalPath, const std::string& mountPoint) {
  if (logicalPath == mountPoint) {
    return true;
  }
  if (logicalPath.size() <= mountPoint.size()) {
    return false;
  }
  return logicalPath.compare(0, mountPoint.size(), mountPoint) == 0 &&
         logicalPath[mountPoint.size()] == '/';
}

const VirtualFileSystem::Mount& VirtualFileSystem::resolveMount(const std::string& logicalPath) const {
  for (const auto& mount : mounts_) {
    if (isMountMatch(logicalPath, mount.mountPoint)) {
      return mount;
    }
  }
  throw std::runtime_error("no mount found for logical path: " + logicalPath);
}

std::string VirtualFileSystem::resolveRelativePath(const std::string& logicalPath, const Mount& mount) const {
  if (logicalPath == mount.mountPoint) {
    return {};
  }
  return normalizeRelativePath(logicalPath.substr(mount.mountPoint.size()));
}

void LegacyPathResolver::bind(std::string variableName, std::string logicalMountPoint) {
  std::lock_guard<std::mutex> lock(*mutex_);
  bindings_[normalizeVariableName(variableName)] = VirtualFileSystem::normalizeLogicalPath(logicalMountPoint);
}

std::string LegacyPathResolver::expand(const std::string& path) const {
  std::lock_guard<std::mutex> lock(*mutex_);
  if (path.empty()) {
    throw std::invalid_argument("path must not be empty");
  }

  if (path.front() == '/') {
    return VirtualFileSystem::normalizeLogicalPath(path);
  }

  const auto [variableName, suffix] = splitVariableReference(path);
  if (variableName.empty()) {
    throw std::runtime_error("expected a logical path or $Variable-prefixed path: " + path);
  }

  const auto it = bindings_.find(variableName);
  if (it == bindings_.end()) {
    throw std::runtime_error("variable is not bound to a mount point: $" + variableName);
  }

  if (suffix.empty()) {
    return it->second;
  }

  return VirtualFileSystem::normalizeLogicalPath(it->second + suffix);
}

void VfsRuntime::mount(std::string mountPoint, std::shared_ptr<IFileSystem> fileSystem) {
  vfs_.mount(std::move(mountPoint), std::move(fileSystem));
}

void VfsRuntime::mount(std::string mountPoint, const std::string& backendType, const BackendOptions& options) {
  vfs_.mount(std::move(mountPoint), backends_.create(backendType, options));
}

void VfsRuntime::mountVariable(std::string variableName, std::string mountPoint,
                               std::shared_ptr<IFileSystem> fileSystem) {
  resolver_.bind(variableName, mountPoint);
  vfs_.mount(std::move(mountPoint), std::move(fileSystem));
}

void VfsRuntime::mountVariable(std::string variableName, std::string mountPoint, const std::string& backendType,
                               const BackendOptions& options) {
  resolver_.bind(variableName, mountPoint);
  vfs_.mount(std::move(mountPoint), backends_.create(backendType, options));
}

void VfsRuntime::bindVariable(std::string variableName, std::string logicalMountPoint) {
  resolver_.bind(std::move(variableName), std::move(logicalMountPoint));
}

std::string VfsRuntime::expandPath(const std::string& path) const {
  return resolver_.expand(path);
}

std::unique_ptr<std::istream> VfsRuntime::openRead(const std::string& path) const {
  return vfs_.openRead(resolver_.expand(path));
}

std::unique_ptr<std::ostream> VfsRuntime::openWrite(const std::string& path) {
  return vfs_.openWrite(resolver_.expand(path));
}

bool VfsRuntime::exists(const std::string& path) const {
  return vfs_.exists(resolver_.expand(path));
}

}  // namespace avfs
