#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "xdl/error.hpp"
#include "xdl/http.hpp"
#include "xdl/options.hpp"
#include "xdl/process.hpp"

namespace xdl {

// Anything longer than this becomes an unreasonably large GIF, so the user
// gets a nudge toward --start/--duration rather than a surprise.
inline constexpr double kLongClipWarningSeconds = 30.0;

struct SourceResult {
  std::string source;
  std::vector<std::filesystem::path> files;
  std::optional<Error> error;
  std::vector<std::string> notes;
};

// Turns tweet references into files on disk. One instance per run; process_one
// is safe to call concurrently from pool workers because it touches no shared
// mutable state beyond the injected clients.
class Downloader {
 public:
  Downloader(HttpClient& http, ProcessRunner& runner, const Options& options)
      : http_(http), runner_(runner), options_(options) {}

  SourceResult process_one(std::string_view source);

  // Fans the sources out across `options.jobs` workers, preserving input order
  // in the returned vector so output is deterministic.
  std::vector<SourceResult> run(std::span<const std::string> sources);

 private:
  HttpClient& http_;
  ProcessRunner& runner_;
  const Options& options_;
};

// Normalises, drops blanks and '#' comments, and removes duplicates while
// preserving first-seen order.
std::vector<std::string> collect_sources(std::span<const std::string> raw);

}  // namespace xdl
