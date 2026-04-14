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

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include "Surelog/Common/FileSystem.h"
#include "Surelog/Common/PathId.h"
#include "Surelog/Common/Session.h"
#include "Surelog/SourceCompile/SymbolTable.h"

#ifdef SURELOG_WITH_ZLIB
#include <zlib.h>
#endif

namespace SURELOG {

namespace fs = std::filesystem;

namespace {

TEST(AvfsFileSystemTest, ReadWriteOperationsGoThroughFilesystemAbstraction) {
  const fs::path testdir = FileSystem::normalize(fs::path(testing::TempDir()) / "avfs-fs");
  const fs::path filepath = testdir / "nested" / "file.sv";

  std::error_code ec;
  fs::remove_all(testdir, ec);

  std::unique_ptr<AvfsFileSystem> fileSystem(new AvfsFileSystem(testdir));
  std::unique_ptr<SymbolTable> symbolTable(new SymbolTable);

  const PathId outputDirId = fileSystem->getOutputDir(testdir.string(), symbolTable.get());
  ASSERT_NE(outputDirId, BadPathId);

  const PathId fileId = fileSystem->toPathId(filepath.string(), symbolTable.get());
  ASSERT_NE(fileId, BadPathId);

  constexpr std::string_view kContent = "module top; endmodule\n";
  EXPECT_TRUE(fileSystem->writeContent(fileId, kContent));

  std::string content;
  EXPECT_TRUE(fileSystem->readContent(fileId, content));
  EXPECT_EQ(content, kContent);
  EXPECT_TRUE(fileSystem->exists(fileId));

  fs::remove_all(testdir, ec);
}

TEST(AvfsFileSystemTest, SessionDefaultsToAvfsFileSystem) {
  Session session;
  EXPECT_NE(dynamic_cast<AvfsFileSystem*>(session.getFileSystem()), nullptr);
}

#ifdef SURELOG_WITH_ZLIB
TEST(AvfsFileSystemTest, ReadCompressedContent) {
  const fs::path testdir = FileSystem::normalize(fs::path(testing::TempDir()) / "avfs-gz-fs");
  const fs::path filepath = testdir / "nested" / "file.sv.gz";
  constexpr std::string_view kContent = "module top;\n  logic value;\nendmodule\n";

  std::error_code ec;
  fs::remove_all(testdir, ec);
  fs::create_directories(filepath.parent_path(), ec);
  ASSERT_FALSE(ec);

  gzFile zippedFile = gzopen(filepath.string().c_str(), "wb");
  ASSERT_NE(zippedFile, nullptr);
  ASSERT_GT(gzwrite(zippedFile, kContent.data(), kContent.size()), 0);
  ASSERT_EQ(gzclose(zippedFile), Z_OK);

  std::unique_ptr<AvfsFileSystem> fileSystem(new AvfsFileSystem(testdir));
  std::unique_ptr<SymbolTable> symbolTable(new SymbolTable);

  const PathId fileId = fileSystem->toPathId(filepath.string(), symbolTable.get());
  ASSERT_NE(fileId, BadPathId);

  std::streamsize size = 0;
  EXPECT_TRUE(fileSystem->filesize(fileId, &size));
  EXPECT_EQ(size, static_cast<std::streamsize>(kContent.size()));

  std::string content;
  EXPECT_TRUE(fileSystem->readContent(fileId, content));
  EXPECT_EQ(content, kContent);

  fs::remove_all(testdir, ec);
}
#endif

}  // namespace

}  // namespace SURELOG
