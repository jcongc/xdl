#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace xdl::testing {

// Fixtures live on disk rather than as string literals so they stay diffable
// and can be re-captured with a script when Twitter changes its payloads.
inline std::string load_fixture(const std::string& name) {
  const std::filesystem::path path = std::filesystem::path{XDL_FIXTURE_DIR} / name;
  std::ifstream in{path, std::ios::binary};
  if (!in) {
    throw std::runtime_error{"missing fixture: " + path.string()};
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace xdl::testing
