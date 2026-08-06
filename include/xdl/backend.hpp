#pragma once

#include <chrono>
#include <span>
#include <string_view>
#include <vector>

#include "xdl/error.hpp"
#include "xdl/media.hpp"
#include "xdl/options.hpp"

namespace xdl {

// A place media URLs can be discovered. Backends are tried in order and the
// first one to return a non-empty, filter-matching list wins.
class MediaSource {
 public:
  virtual ~MediaSource() = default;
  virtual std::string_view name() const = 0;
  virtual Result<std::vector<Media>> fetch(std::string_view tweet_id,
                                           std::chrono::milliseconds timeout) = 0;
};

std::vector<Media> filter_media(const std::vector<Media>& media, OnlyFilter only);

// Tries each source in turn. A source that throws, errors, or returns nothing
// matching the filter is recorded and the next is tried; if all fail, the
// individual reasons are joined with "; " so the message names every attempt.
Result<std::vector<Media>> resolve_media(std::span<MediaSource* const> sources,
                                         std::string_view tweet_id,
                                         OnlyFilter only,
                                         std::chrono::milliseconds timeout);

}  // namespace xdl
