cmake_minimum_required(VERSION 3.28)

project(xdl-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(XDL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(XDL_BUILD_TESTS "Build the test suite" ON)

include(cmake/Warnings.cmake)

find_package(CURL REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(simdjson REQUIRED IMPORTED_TARGET simdjson)

add_library(xdl_core STATIC
  src/url.cpp
  src/token.cpp
  src/ffmpeg.cpp
  src/process.cpp
  src/http.cpp
  src/syndication.cpp
  src/ytdlp.cpp
  src/backend.cpp
  src/thread_pool.cpp
  src/downloader.cpp
  src/cli.cpp
)
target_include_directories(xdl_core PUBLIC include)
target_link_libraries(xdl_core
  PUBLIC  xdl_warnings
  PRIVATE CURL::libcurl PkgConfig::simdjson
)

add_executable(xdl app/main.cpp)
target_link_libraries(xdl PRIVATE xdl_core)

if(XDL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
