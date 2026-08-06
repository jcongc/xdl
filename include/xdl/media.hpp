#pragma once

#include <string>
#include <string_view>

namespace xdl {

// Twitter stores "GIFs" as silent looping MP4s (media type `animated_gif`) and
// regular videos as `video`. Both arrive as MP4; only the semantics differ.
enum class MediaKind { Gif, Video };

struct Media {
  std::string url;
  MediaKind kind{MediaKind::Video};
  int duration_ms{0};
};

constexpr std::string_view to_string(MediaKind kind) {
  return kind == MediaKind::Gif ? "gif" : "video";
}

}  // namespace xdl
