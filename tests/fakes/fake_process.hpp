#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "xdl/process.hpp"

namespace xdl::testing {

// Replays canned subprocess results in order, recording every argv it was
// handed so tests can assert on command construction.
class FakeProcessRunner final : public ProcessRunner {
 public:
  void push_result(ProcessResult result) { queued_.push_back(std::move(result)); }
  void push_error(Error error) { errors_[queued_.size()] = std::move(error); }

  Result<ProcessResult> run(std::span<const std::string> argv,
                            std::chrono::milliseconds) override {
    calls.emplace_back(argv.begin(), argv.end());
    const size_t index = calls.size() - 1;

    if (const auto it = errors_.find(index); it != errors_.end()) {
      return std::unexpected(it->second);
    }
    if (index < queued_.size()) {
      return queued_[index];
    }
    return ProcessResult{0, "", ""};
  }

  std::vector<std::vector<std::string>> calls;

 private:
  std::vector<ProcessResult> queued_;
  std::map<size_t, Error> errors_;
};

}  // namespace xdl::testing
