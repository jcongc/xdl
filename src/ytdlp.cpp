#include "xdl/ytdlp.hpp"

#include <simdjson.h>

#include <string>
#include <vector>

namespace xdl {
namespace {

struct FormatPick {
  std::string url;
  double tbr{-1.0};
  int64_t height{-1};
};

// Best plain-HTTPS format that actually carries video. HLS is skipped so we
// end up with a single file rather than a playlist.
void consider_formats(simdjson::ondemand::array formats, FormatPick& best) {
  for (auto format : formats) {
    std::string_view vcodec;
    if (format["vcodec"].get_string().get(vcodec) == simdjson::SUCCESS &&
        vcodec == "none") {
      continue;
    }

    std::string_view protocol;
    if (format["protocol"].get_string().get(protocol) == simdjson::SUCCESS &&
        protocol != "https" && protocol != "http") {
      continue;
    }

    std::string_view url;
    if (format["url"].get_string().get(url) != simdjson::SUCCESS) {
      continue;
    }

    double tbr = 0.0;
    if (format["tbr"].get_double().get(tbr) != simdjson::SUCCESS) {
      tbr = 0.0;
    }
    int64_t height = 0;
    if (format["height"].get_int64().get(height) != simdjson::SUCCESS) {
      height = 0;
    }

    if (tbr > best.tbr || (tbr == best.tbr && height > best.height)) {
      best.tbr = tbr;
      best.height = height;
      best.url = std::string{url};  // copied before the parser advances
    }
  }
}

Result<void> collect_entry(simdjson::ondemand::object entry, std::vector<Media>& out) {
  FormatPick best;

  simdjson::ondemand::array formats;
  if (entry["formats"].get_array().get(formats) == simdjson::SUCCESS) {
    consider_formats(formats, best);
  }

  if (best.url.empty()) {
    std::string_view direct;
    if (entry["url"].get_string().get(direct) == simdjson::SUCCESS) {
      best.url = std::string{direct};
    }
  }
  if (best.url.empty()) {
    return {};
  }

  // yt-dlp does not expose Twitter's animated_gif flag, and it reports acodec
  // as null for progressive Twitter formats whether or not there is audio — so
  // "no audio codec" cannot distinguish the two kinds and would classify every
  // video as a GIF.
  //
  // The CDN path is the dependable signal: animated GIFs are served from
  // /tweet_video/, while real videos come from /amplify_video/ or
  // /ext_tw_video/.
  bool is_gif = best.url.find("/tweet_video/") != std::string::npos;

  // An explicit acodec of "none" is still trusted when yt-dlp provides one.
  if (!is_gif) {
    std::string_view acodec;
    if (entry["acodec"].get_string().get(acodec) == simdjson::SUCCESS &&
        acodec == "none") {
      is_gif = true;
    }
  }

  double duration_s = 0.0;
  if (entry["duration"].get_double().get(duration_s) != simdjson::SUCCESS) {
    duration_s = 0.0;
  }

  out.push_back(Media{std::move(best.url),
                      is_gif ? MediaKind::Gif : MediaKind::Video,
                      static_cast<int>(duration_s * 1000.0)});
  return {};
}

}  // namespace

Result<std::vector<Media>> parse_ytdlp_payload(std::string_view json) {
  simdjson::padded_string padded{json};
  simdjson::ondemand::parser parser;
  simdjson::ondemand::document doc;

  if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
    return fail(Error::Kind::Subprocess, "yt-dlp returned malformed JSON");
  }

  // Materialise the root object before touching any field: iterate() is lazy,
  // so this is where malformed input surfaces as an error rather than as a
  // crash further down.
  simdjson::ondemand::object root;
  if (doc.get_object().get(root) != simdjson::SUCCESS) {
    return fail(Error::Kind::Subprocess, "yt-dlp returned malformed JSON");
  }

  std::vector<Media> out;

  simdjson::ondemand::array entries;
  if (root["entries"].get_array().get(entries) == simdjson::SUCCESS) {
    for (auto entry : entries) {
      simdjson::ondemand::object object;
      if (entry.get_object().get(object) != simdjson::SUCCESS) {
        continue;
      }
      auto rc = collect_entry(object, out);
      if (!rc) {
        return std::unexpected(rc.error());
      }
    }
    return out;
  }

  // A single tweet dumps one object rather than a playlist.
  auto rc = collect_entry(root, out);
  if (!rc) {
    return std::unexpected(rc.error());
  }
  return out;
}

Result<std::vector<Media>> YtDlpSource::fetch(std::string_view tweet_id,
                                              std::chrono::milliseconds timeout) {
  if (!program_exists("yt-dlp")) {
    return fail(Error::Kind::Subprocess, "yt-dlp not installed (pip install yt-dlp)");
  }

  const std::vector<std::string> argv{
      "yt-dlp", "--dump-single-json", "--no-warnings", "--no-playlist",
      "https://x.com/i/status/" + std::string{tweet_id}};

  auto result = runner_.run(argv, timeout);
  if (!result) {
    return std::unexpected(result.error());
  }
  if (result->exit_code != 0) {
    std::string detail = result->err;
    // yt-dlp's useful complaint is on the last line of stderr.
    if (const auto nl = detail.find_last_not_of("\n"); nl != std::string::npos) {
      detail.erase(nl + 1);
    }
    if (const auto nl = detail.rfind('\n'); nl != std::string::npos) {
      detail = detail.substr(nl + 1);
    }
    return fail(Error::Kind::Subprocess, "yt-dlp failed: " + detail);
  }

  return parse_ytdlp_payload(result->out);
}

}  // namespace xdl
