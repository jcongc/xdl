#pragma once

#include <chrono>
#include <span>
#include <string>

#include "xdl/error.hpp"

namespace xdl {

struct ProcessResult {
  int exit_code{0};
  std::string out;
  std::string err;
};

// The second seam. ffmpeg and yt-dlp both reach the outside world through
// here, so tests can drive them with canned output.
class ProcessRunner {
 public:
  virtual ~ProcessRunner() = default;

  // argv[0] is the program. Never goes through a shell, so URLs and filenames
  // cannot inject commands.
  virtual Result<ProcessResult> run(std::span<const std::string> argv,
                                    std::chrono::milliseconds timeout) = 0;
};

class PosixProcessRunner final : public ProcessRunner {
 public:
  Result<ProcessResult> run(std::span<const std::string> argv,
                            std::chrono::milliseconds timeout) override;
};

// True when `program` resolves on PATH. Used to produce a clear "install
// ffmpeg" message instead of a spawn failure.
bool program_exists(const std::string& program);

}  // namespace xdl
