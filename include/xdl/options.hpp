#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string_view>

namespace xdl {

enum class OnlyFilter { All, Gif, Video };
enum class BackendChoice { Auto, Syndication, YtDlp };

// Every default here mirrors the Python tool exactly.
struct Options {
  std::filesystem::path outdir;
  OnlyFilter only{OnlyFilter::All};
  bool make_gif{false};
  bool keep_mp4{true};
  int fps{15};
  int width{480};
  double start{0.0};
  std::optional<double> duration;
  BackendChoice backend{BackendChoice::Auto};
  unsigned jobs{4};
  bool overwrite{false};
  std::chrono::milliseconds timeout{30000};
};

// Human-readable name of what a filter is looking for, used to build error
// messages that read like the Python's ("no animated GIF in tweet").
constexpr std::string_view describe(OnlyFilter only) {
  switch (only) {
    case OnlyFilter::All:   return "video or GIF";
    case OnlyFilter::Gif:   return "animated GIF";
    case OnlyFilter::Video: return "video";
  }
  return "media";
}

}  // namespace xdl
