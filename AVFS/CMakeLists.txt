cmake_minimum_required(VERSION 3.16)
project(AVFS LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(avfs
  src/Discovery.cpp
  src/Vfs.cpp
)

add_library(AVFS::avfs ALIAS avfs)

target_include_directories(avfs
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(avfs
  PUBLIC
    OpenSSL::Crypto
    ZLIB::ZLIB
)

set_target_properties(avfs PROPERTIES
  EXPORT_NAME avfs
)

add_executable(avfs_tests
  tests/VfsTests.cpp
)

target_link_libraries(avfs_tests
  PRIVATE
    avfs
)

add_subdirectory(examples/abc)

enable_testing()
add_test(NAME avfs_tests COMMAND avfs_tests)

install(TARGETS avfs
  EXPORT AVFSTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/AVFSConfigVersion.cmake
  VERSION 1.0.0
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AVFSConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/AVFSConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AVFS
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/AVFSConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/AVFSConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AVFS
)

install(EXPORT AVFSTargets
  FILE AVFSTargets.cmake
  NAMESPACE AVFS::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AVFS
)
