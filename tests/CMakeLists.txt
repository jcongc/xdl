include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(xdl_tests
  url_test.cpp
  token_test.cpp
  ffmpeg_test.cpp
  process_test.cpp
  syndication_test.cpp
  ytdlp_test.cpp
  backend_test.cpp
  thread_pool_test.cpp
  downloader_test.cpp
  cli_test.cpp
)

target_include_directories(xdl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xdl_tests PRIVATE xdl_core GTest::gtest_main)

# Tests read captured payloads from here rather than embedding huge string
# literals, so the fixtures stay diffable and easy to re-capture.
target_compile_definitions(xdl_tests PRIVATE
  XDL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

include(GoogleTest)
gtest_discover_tests(xdl_tests)
