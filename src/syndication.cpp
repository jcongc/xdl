#include "xdl/syndication.hpp"

#include <simdjson.h>

#include <string>
#include <vector>

#include "xdl/token.hpp"

namespace xdl {
namespace {

// Syndication media types that carry something downloadable. Photos are
// deliberately absent.
MediaKind kind_from_type(std::string_view type, bool& downloadable) {
  downloadable = true;
  if (type == "animated_gif") {
    return MediaKind::Gif;
  }
  if (type == "video") {
    return MediaKind::Video;
  }
  downloadable = false;
  return MediaKind::Video;
}

// Highest-bitrate progressive MP4. HLS playlists are skipped: they are not a
// single file and ffmpeg would need a second fetch to use them.
std::string best_mp4(simdjson::ondemand::array variants) {
  std::string best_url;
  int64_t best_bitrate = -1;

  for (auto variant : variants) {
    std::string_view content_type;
    if (variant["content_type"].get_string().get(content_type) != simdjson::SUCCESS) {
      continue;
    }
    if (content_type != "video/mp4") {
      continue;
    }

    std::string_view url;
    if (variant["url"].get_string().get(url) != simdjson::SUCCESS) {
      continue;
    }

    int64_t bitrate = 0;
    if (variant["bitrate"].get_int64().get(bitrate) != simdjson::SUCCESS) {
      bitrate = 0;
    }

    if (bitrate > best_bitrate) {
      best_bitrate = bitrate;
      best_url = std::string{url};  // copied out before the parser moves on
    }
  }
  return best_url;
}

}  // namespace

std::string build_syndication_url(std::string_view tweet_id) {
  std::string url{kSyndicationUrl};
  url += "?id=";
  url += tweet_id;
  url += "&lang=en&features=&token=";
  url += syndication_token(tweet_id);
  return url;
}

Result<std::vector<Media>> parse_syndication_payload(std::string_view json,
                                                     std::string_view tweet_id) {
  // simdjson requires padded input; everything below is copied into owned
  // values before this buffer goes out of scope, so no view escapes.
  simdjson::padded_string padded{json};
  simdjson::ondemand::parser parser;
  simdjson::ondemand::document doc;

  if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
    return fail(Error::Kind::Network, "syndication returned malformed JSON");
  }

  // iterate() is lazy — it does not touch the buffer, so malformed input is
  // only detected once something is actually read. Materialising the root
  // object here is what turns "{not json" into an error instead of a crash
  // deep inside a later field lookup.
  simdjson::ondemand::object root;
  if (doc.get_object().get(root) != simdjson::SUCCESS) {
    return fail(Error::Kind::Network, "syndication returned malformed JSON");
  }

  // Field lookups below go through find_field_unordered, which rewinds the
  // object internally, so order does not matter and no explicit rewind of the
  // document is needed (or safe).
  std::string_view typename_field;
  const bool tombstoned =
      (root["__typename"].get_string().get(typename_field) == simdjson::SUCCESS &&
       typename_field == "TweetTombstone") ||
      root["tombstone"].error() == simdjson::SUCCESS;
  if (tombstoned) {
    return fail(Error::Kind::Unavailable,
                "tweet " + std::string{tweet_id} +
                    " is unavailable (deleted/protected/age-gated)");
  }

  simdjson::ondemand::array details;
  if (root["mediaDetails"].get_array().get(details) != simdjson::SUCCESS) {
    return std::vector<Media>{};
  }

  std::vector<Media> out;
  for (auto item : details) {
    std::string_view type;
    if (item["type"].get_string().get(type) != simdjson::SUCCESS) {
      continue;
    }
    bool downloadable = false;
    const MediaKind kind = kind_from_type(type, downloadable);
    if (!downloadable) {
      continue;
    }

    simdjson::ondemand::object video_info;
    if (item["video_info"].get_object().get(video_info) != simdjson::SUCCESS) {
      continue;
    }

    int64_t duration_ms = 0;
    if (video_info["duration_millis"].get_int64().get(duration_ms) != simdjson::SUCCESS) {
      duration_ms = 0;
    }

    simdjson::ondemand::array variants;
    if (video_info["variants"].get_array().get(variants) != simdjson::SUCCESS) {
      continue;
    }

    std::string url = best_mp4(variants);
    if (url.empty()) {
      continue;
    }
    // Drop the CDN query string so repeated runs produce identical filenames.
    if (const auto q = url.find('?'); q != std::string::npos) {
      url.erase(q);
    }

    out.push_back(Media{std::move(url), kind, static_cast<int>(duration_ms)});
  }

  return out;
}

Result<std::vector<Media>> SyndicationSource::fetch(std::string_view tweet_id,
                                                    std::chrono::milliseconds timeout) {
  const Headers headers{
      {"Accept", "application/json"},
      {"Referer", "https://platform.twitter.com/"},
  };

  auto response = http_.get(build_syndication_url(tweet_id), headers, timeout);
  if (!response) {
    return std::unexpected(response.error());
  }
  if (response->status == 404) {
    return fail(Error::Kind::Unavailable,
                "tweet " + std::string{tweet_id} + " not found");
  }
  if (response->status < 200 || response->status >= 300) {
    return fail(Error::Kind::Network,
                "syndication HTTP " + std::to_string(response->status) + " for " +
                    std::string{tweet_id});
  }

  return parse_syndication_payload(response->body, tweet_id);
}

}  // namespace xdl
