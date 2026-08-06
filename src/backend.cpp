#include "xdl/backend.hpp"

#include <exception>
#include <string>
#include <vector>

namespace xdl {

std::vector<Media> filter_media(const std::vector<Media>& media, OnlyFilter only) {
  if (only == OnlyFilter::All) {
    return media;
  }
  const MediaKind wanted = (only == OnlyFilter::Gif) ? MediaKind::Gif : MediaKind::Video;

  std::vector<Media> out;
  for (const auto& item : media) {
    if (item.kind == wanted) {
      out.push_back(item);
    }
  }
  return out;
}

Result<std::vector<Media>> resolve_media(std::span<MediaSource* const> sources,
                                         std::string_view tweet_id,
                                         OnlyFilter only,
                                         std::chrono::milliseconds timeout) {
  std::vector<std::string> problems;

  for (MediaSource* source : sources) {
    if (source == nullptr) {
      continue;
    }

    // One misbehaving backend must not take down the whole batch, so even
    // unexpected exceptions are folded into the accumulated reasons.
    try {
      auto fetched = source->fetch(tweet_id, timeout);
      if (!fetched) {
        problems.push_back(std::string{source->name()} + ": " + fetched.error().message);
        continue;
      }

      auto filtered = filter_media(*fetched, only);
      if (!filtered.empty()) {
        return filtered;
      }
      problems.push_back(std::string{source->name()} + ": no " +
                         std::string{describe(only)} + " in tweet");
    } catch (const std::exception& e) {
      problems.push_back(std::string{source->name()} + ": " + e.what());
    }
  }

  if (problems.empty()) {
    return fail(Error::Kind::NoMedia, "no backends available");
  }

  std::string joined;
  for (size_t i = 0; i < problems.size(); ++i) {
    if (i > 0) {
      joined += "; ";
    }
    joined += problems[i];
  }
  return fail(Error::Kind::NoMedia, joined);
}

}  // namespace xdl
