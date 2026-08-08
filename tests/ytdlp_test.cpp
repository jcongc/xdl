#include "xdl/ytdlp.hpp"

#include <gtest/gtest.h>

#include <string>

#include "fakes/fake_process.hpp"
#include "fixture_loader.hpp"

namespace {

using xdl::Error;
using xdl::MediaKind;
using xdl::parse_ytdlp_payload;
using xdl::testing::FakeProcessRunner;
using xdl::testing::load_fixture;

// Real capture from `yt-dlp --dump-single-json` against the NASA video tweet.
// Regression: yt-dlp reports acodec as null here, so the old "no audio codec
// means silent means GIF" rule classified this — and every other real video
// reaching the fallback — as a GIF, which made --only gif download videos.
TEST(YtDlpPayload, ParsesRealDump) {
  auto media = parse_ytdlp_payload(load_fixture("ytdlp_video.json"));
  ASSERT_TRUE(media.has_value()) << media.error().message;
  ASSERT_FALSE(media->empty());
  EXPECT_TRUE((*media)[0].url.starts_with("https://"));
  EXPECT_GT((*media)[0].duration_ms, 0);
  EXPECT_EQ((*media)[0].kind, MediaKind::Video)
      << "a real video must not be classified as a GIF";
}

// Twitter serves animated GIFs from a distinct CDN path, which is the only
// dependable signal since acodec is null for both kinds.
TEST(YtDlpPayload, ClassifiesTweetVideoPathsAsGif) {
  const std::string json = R"({
    "formats": [{"url": "https://video.twimg.com/tweet_video/ClwOxLQ.mp4",
                 "protocol": "https", "vcodec": "avc1", "tbr": 1.0}]
  })";
  auto media = parse_ytdlp_payload(json);
  ASSERT_TRUE(media.has_value());
  ASSERT_EQ(media->size(), 1u);
  EXPECT_EQ((*media)[0].kind, MediaKind::Gif);
}

TEST(YtDlpPayload, ClassifiesAmplifyAndExtVideoPathsAsVideo) {
  for (const auto* url : {"https://video.twimg.com/amplify_video/1/vid/a.mp4",
                          "https://video.twimg.com/ext_tw_video/1/pu/vid/a.mp4"}) {
    const std::string json = std::string{R"({"formats": [{"url": ")"} + url +
                             R"(", "protocol": "https", "vcodec": "avc1", "tbr": 1.0}]})";
    auto media = parse_ytdlp_payload(json);
    ASSERT_TRUE(media.has_value()) << url;
    ASSERT_EQ(media->size(), 1u) << url;
    EXPECT_EQ((*media)[0].kind, MediaKind::Video) << url;
  }
}

// With no usable signal at all, video is the safer assumption: mislabelling a
// GIF as a video only affects --only filtering, whereas the reverse made
// --only gif download multi-megabyte videos.
TEST(YtDlpPayload, DefaultsToVideoWhenAudioIsUnknown) {
  const std::string json = R"({
    "formats": [{"url": "https://cdn/unknown.mp4", "protocol": "https",
                 "vcodec": "avc1", "tbr": 1.0}]
  })";
  auto media = parse_ytdlp_payload(json);
  ASSERT_TRUE(media.has_value());
  ASSERT_EQ(media->size(), 1u);
  EXPECT_EQ((*media)[0].kind, MediaKind::Video);
}

TEST(YtDlpPayload, PrefersHighestBitrateProgressiveFormat) {
  const std::string json = R"({
    "acodec": "mp4a.40.2",
    "duration": 12.5,
    "formats": [
      {"url": "https://cdn/low.mp4",  "protocol": "https", "vcodec": "avc1", "tbr": 320.0, "height": 360},
      {"url": "https://cdn/high.mp4", "protocol": "https", "vcodec": "avc1", "tbr": 2176.0, "height": 720},
      {"url": "https://cdn/mid.mp4",  "protocol": "https", "vcodec": "avc1", "tbr": 832.0, "height": 480}
    ]
  })";
  auto media = parse_ytdlp_payload(json);
  ASSERT_TRUE(media.has_value()) << media.error().message;
  ASSERT_EQ(media->size(), 1u);
  EXPECT_EQ((*media)[0].url, "https://cdn/high.mp4");
  EXPECT_EQ((*media)[0].duration_ms, 12500);
}

TEST(YtDlpPayload, SkipsHlsAndAudioOnlyFormats) {
  const std::string json = R"({
    "acodec": "mp4a.40.2",
    "formats": [
      {"url": "https://cdn/playlist.m3u8", "protocol": "m3u8_native", "vcodec": "avc1", "tbr": 9999.0},
      {"url": "https://cdn/audio.m4a",     "protocol": "https", "vcodec": "none", "tbr": 5000.0},
      {"url": "https://cdn/video.mp4",     "protocol": "https", "vcodec": "avc1", "tbr": 100.0}
    ]
  })";
  auto media = parse_ytdlp_payload(json);
  ASSERT_TRUE(media.has_value());
  ASSERT_EQ(media->size(), 1u);
  EXPECT_EQ((*media)[0].url, "https://cdn/video.mp4");
}

// yt-dlp exposes no animated_gif flag, so silence is the only available
// signal. Documented heuristic, inherited from the Python tool.
TEST(YtDlpPayload, ClassifiesSilentStreamsAsGif) {
  const std::string json = R"({
    "acodec": "none",
    "formats": [{"url": "https://cdn/a.mp4", "protocol": "https", "vcodec": "avc1", "tbr": 1.0}]
  })";
  auto media = parse_ytdlp_payload(json);
  ASSERT_TRUE(media.has_value());
  ASSERT_EQ(media->size(), 1u);
  EXPECT_EQ((*media)[0].kind, MediaKind::Gif);
}

TEST(YtDlpPayload, ClassifiesStreamsWithAudioAsVideo) {
  const std::string json = R"({
    "acodec": "mp4a.40.2",
    "formats": [{"url": "https://cdn/a.mp4", "protocol": "https", "vcodec": "avc1", "tbr": 1.0}]
  })";
  auto media = parse_ytdlp_payload(json);
  ASSERT_TRUE(media.has_value());
  ASSERT_EQ(media->size(), 1u);
  EXPECT_EQ((*media)[0].kind, MediaKind::Video);
}

TEST(YtDlpPayload, FallsBackToTopLevelUrlWhenNoFormats) {
  const std::string json = R"({"acodec": "none", "url": "https://cdn/direct.mp4"})";
  auto media = parse_ytdlp_payload(json);
  ASSERT_TRUE(media.has_value());
  ASSERT_EQ(media->size(), 1u);
  EXPECT_EQ((*media)[0].url, "https://cdn/direct.mp4");
}

TEST(YtDlpPayload, HandlesPlaylistEntries) {
  const std::string json = R"({
    "entries": [
      {"acodec": "none",       "url": "https://cdn/one.mp4"},
      {"acodec": "mp4a.40.2",  "url": "https://cdn/two.mp4"}
    ]
  })";
  auto media = parse_ytdlp_payload(json);
  ASSERT_TRUE(media.has_value());
  ASSERT_EQ(media->size(), 2u);
  EXPECT_EQ((*media)[0].kind, MediaKind::Gif);
  EXPECT_EQ((*media)[1].kind, MediaKind::Video);
}

TEST(YtDlpSource, ReportsNonZeroExitAsSubprocessFailure) {
  FakeProcessRunner runner;
  runner.push_result(xdl::ProcessResult{1, "", "ERROR: Unable to extract data\n"});

  xdl::YtDlpSource source{runner};
  auto media = source.fetch("123", std::chrono::milliseconds{1000});

  // Skipped when yt-dlp is absent, since the source short-circuits before it
  // ever reaches the runner.
  if (!xdl::program_exists("yt-dlp")) {
    GTEST_SKIP() << "yt-dlp not installed";
  }
  ASSERT_FALSE(media.has_value());
  EXPECT_EQ(media.error().kind, Error::Kind::Subprocess);
  EXPECT_NE(media.error().message.find("Unable to extract data"), std::string::npos);
}

TEST(YtDlpSource, PassesTheExpectedArgv) {
  if (!xdl::program_exists("yt-dlp")) {
    GTEST_SKIP() << "yt-dlp not installed";
  }
  FakeProcessRunner runner;
  runner.push_result(xdl::ProcessResult{
      0, R"({"acodec": "none", "url": "https://cdn/a.mp4"})", ""});

  xdl::YtDlpSource source{runner};
  auto media = source.fetch("999", std::chrono::milliseconds{1000});
  ASSERT_TRUE(media.has_value()) << media.error().message;

  ASSERT_EQ(runner.calls.size(), 1u);
  const auto& argv = runner.calls[0];
  EXPECT_EQ(argv[0], "yt-dlp");
  EXPECT_NE(std::find(argv.begin(), argv.end(), "--dump-single-json"), argv.end());
  EXPECT_EQ(argv.back(), "https://x.com/i/status/999");
}

}  // namespace
