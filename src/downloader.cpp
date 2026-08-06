#include "xdl/downloader.hpp"

#include <algorithm>
#include <exception>
#include <format>
#include <future>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "xdl/backend.hpp"
#include "xdl/ffmpeg.hpp"
#include "xdl/syndication.hpp"
#include "xdl/thread_pool.hpp"
#include "xdl/url.hpp"
#include "xdl/ytdlp.hpp"

namespace xdl {
namespace {

bool is_comment_or_blank(std::string_view line) {
  return line.empty() || line.front() == '#';
}

std::string_view trimmed(std::string_view s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

}  // namespace

std::vector<std::string> collect_sources(std::span<const std::string> raw) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;

  for (const auto& entry : raw) {
    const auto trimmed_entry = trimmed(entry);
    if (is_comment_or_blank(trimmed_entry)) {
      continue;
    }
    // Normalising before the dedup check means ?s=20 and ?t=… spellings of the
    // same tweet collapse to one download.
    std::string normalized = normalize_url(trimmed_entry);
    if (normalized.empty() || !seen.insert(normalized).second) {
      continue;
    }
    out.push_back(std::move(normalized));
  }
  return out;
}

SourceResult Downloader::process_one(std::string_view source) {
  SourceResult result;
  result.source = normalize_url(source);

  auto tweet_id = extract_tweet_id(result.source);
  if (!tweet_id) {
    result.error = tweet_id.error();
    return result;
  }

  SyndicationSource syndication{http_};
  YtDlpSource ytdlp{runner_};

  std::vector<MediaSource*> sources;
  switch (options_.backend) {
    case BackendChoice::Auto:
      sources = {&syndication, &ytdlp};
      break;
    case BackendChoice::Syndication:
      sources = {&syndication};
      break;
    case BackendChoice::YtDlp:
      sources = {&ytdlp};
      break;
  }

  auto media = resolve_media(sources, *tweet_id, options_.only, options_.timeout);
  if (!media) {
    result.error = media.error();
    return result;
  }

  std::error_code ec;
  std::filesystem::create_directories(options_.outdir, ec);
  if (ec) {
    result.error = Error{Error::Kind::Io, "cannot create " + options_.outdir.string()};
    return result;
  }

  for (size_t i = 0; i < media->size(); ++i) {
    const Media& item = (*media)[i];
    const std::string stem =
        media->size() == 1 ? *tweet_id : std::format("{}_{}", *tweet_id, i + 1);

    const auto mp4_path = options_.outdir / (stem + ".mp4");
    const auto gif_path = options_.outdir / (stem + ".gif");
    const auto& final_path = options_.make_gif ? gif_path : mp4_path;

    if (std::filesystem::exists(final_path) && !options_.overwrite) {
      result.files.push_back(final_path);
      continue;
    }

    if (!std::filesystem::exists(mp4_path) || options_.overwrite) {
      auto downloaded = http_.download(item.url, mp4_path, options_.timeout);
      if (!downloaded) {
        result.error = downloaded.error();
        return result;
      }
    }

    if (!options_.make_gif) {
      result.files.push_back(mp4_path);
      continue;
    }

    GifSpec spec;
    spec.fps = options_.fps;
    spec.width = options_.width;
    spec.start = options_.start;
    spec.duration = options_.duration;

    const double clip_seconds =
        options_.duration.value_or(static_cast<double>(item.duration_ms) / 1000.0 -
                                   options_.start);
    if (item.duration_ms > 0 && clip_seconds > kLongClipWarningSeconds) {
      result.notes.push_back(std::format(
          "{}: converting {:.0f}s of video to GIF — expect a huge file; "
          "consider --start/--duration",
          mp4_path.filename().string(), clip_seconds));
    }

    auto encoded = encode_gif(runner_, mp4_path, gif_path, spec, options_.timeout);
    if (!encoded) {
      result.error = encoded.error();
      return result;
    }

    result.files.push_back(gif_path);
    if (options_.keep_mp4) {
      result.files.push_back(mp4_path);
    } else {
      std::filesystem::remove(mp4_path, ec);
    }
  }

  return result;
}

std::vector<SourceResult> Downloader::run(std::span<const std::string> sources) {
  std::vector<SourceResult> results(sources.size());
  if (sources.empty()) {
    return results;
  }

  ThreadPool pool{options_.jobs};
  std::vector<std::future<SourceResult>> futures;
  futures.reserve(sources.size());

  for (const auto& source : sources) {
    futures.push_back(pool.submit([this, &source] { return process_one(source); }));
  }

  // Results are stored by index rather than completion order, so output stays
  // deterministic no matter how the workers interleave.
  for (size_t i = 0; i < futures.size(); ++i) {
    try {
      results[i] = futures[i].get();
    } catch (const std::exception& e) {
      results[i].source = sources[i];
      results[i].error = Error{Error::Kind::Io, e.what()};
    }
  }
  return results;
}

}  // namespace xdl
