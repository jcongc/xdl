#pragma once

#include <span>
#include <string>
#include <vector>

#include "xdl/error.hpp"
#include "xdl/options.hpp"

namespace xdl {

struct ParsedArgs {
  Options options;
  std::vector<std::string> sources;  // raw, before normalisation/dedup
  bool help{false};
};

// argv without the program name. Flag names and defaults mirror the Python
// tool exactly, so muscle memory carries over.
Result<ParsedArgs> parse_args(std::span<const std::string> args);

std::string usage();

// Reads one source per line, skipping blanks and '#' comments.
Result<std::vector<std::string>> read_sources_file(const std::string& path);

std::vector<std::string> read_sources_stdin();

}  // namespace xdl
