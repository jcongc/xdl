#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "xdl/backend.hpp"
#include "xdl/error.hpp"
#include "xdl/http.hpp"
#include "xdl/media.hpp"

namespace xdl {

inline constexpr std::string_view kSyndicationUrl =
    "https://cdn.syndication.twimg.com/tweet-result";

// The public embed endpoint: no auth, no API key, no rate limit worth
// worrying about. Just needs the widget's computed token.
class SyndicationSource final : public MediaSource {
 public:
  explicit SyndicationSource(HttpClient& http) : http_(http) {}

  std::string_view name() const override { return "syndication"; }

  Result<std::vector<Media>> fetch(std::string_view tweet_id,
                                   std::chrono::milliseconds timeout) override;

 private:
  HttpClient& http_;
};

// Exposed for testing against captured payloads without any HTTP involved.
Result<std::vector<Media>> parse_syndication_payload(std::string_view json,
                                                     std::string_view tweet_id);

// Builds the fully-qualified request URL including the computed token.
std::string build_syndication_url(std::string_view tweet_id);

}  // namespace xdl
