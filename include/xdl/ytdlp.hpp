#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "xdl/backend.hpp"
#include "xdl/error.hpp"
#include "xdl/media.hpp"
#include "xdl/process.hpp"

namespace xdl {

// Fallback for when Twitter changes the syndication payload, or for tweets
// the embed endpoint won't serve.
class YtDlpSource final : public MediaSource {
 public:
  explicit YtDlpSource(ProcessRunner& runner) : runner_(runner) {}

  std::string_view name() const override { return "yt-dlp"; }

  Result<std::vector<Media>> fetch(std::string_view tweet_id,
                                   std::chrono::milliseconds timeout) override;

 private:
  ProcessRunner& runner_;
};

// Exposed for testing against a captured --dump-single-json payload.
Result<std::vector<Media>> parse_ytdlp_payload(std::string_view json);

}  // namespace xdl
