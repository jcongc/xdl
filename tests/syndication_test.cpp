#include "xdl/syndication.hpp"

#include <gtest/gtest.h>

#include <string>

#include "fakes/fake_http.hpp"
#include "fixture_loader.hpp"

namespace {

using xdl::Error;
using xdl::MediaKind;
using xdl::parse_syndication_payload;
using xdl::testing::FakeHttpClient;
using xdl::testing::load_fixture;

// Real capture: NASA's Parker Solar Probe post, a ~205-second video. The
// payload reports 204871 ms; the muxed MP4 is fractionally longer (ffprobe
// says 204.885 s), so this asserts what the API says, not what ffprobe says.
TEST(SyndicationPayload, ExtractsVideoWithDuration) {
  auto media = parse_syndication_payload(load_fixture("video.json"), "1491475671058681863");
  ASSERT_TRUE(media.has_value()) << media.error().message;
  ASSERT_EQ(media->size(), 1u);
  EXPECT_EQ((*media)[0].kind, MediaKind::Video);
  EXPECT_EQ((*media)[0].duration_ms, 204871);
  EXPECT_TRUE((*media)[0].url.starts_with("https://"));
  EXPECT_TRUE((*media)[0].url.ends_with(".mp4"));
}

// Real capture: a tweet carrying a genuine animated_gif.
TEST(SyndicationPayload, ExtractsAnimatedGif) {
  auto media = parse_syndication_payload(load_fixture("animated_gif.json"), "746487912313688067");
  ASSERT_TRUE(media.has_value()) << media.error().message;
  ASSERT_EQ(media->size(), 1u);
  EXPECT_EQ((*media)[0].kind, MediaKind::Gif);
  EXPECT_TRUE((*media)[0].url.ends_with(".mp4"));
}

// Real capture: the tweet from the original bug report — photo only, so there
// is nothing downloadable regardless of the query string on the URL.
TEST(SyndicationPayload, YieldsNothingForPhotoOnlyTweets) {
  auto media = parse_syndication_payload(load_fixture("photo_only.json"), "2085077475109769243");
  ASSERT_TRUE(media.has_value()) << media.error().message;
  EXPECT_TRUE(media->empty());
}

TEST(SyndicationPayload, ReportsTombstonedTweetsAsUnavailable) {
  auto media = parse_syndication_payload(load_fixture("tombstone.json"), "123");
  ASSERT_FALSE(media.has_value());
  EXPECT_EQ(media.error().kind, Error::Kind::Unavailable);
  EXPECT_NE(media.error().message.find("unavailable"), std::string::npos);
}

TEST(SyndicationPayload, RejectsMalformedJson) {
  auto media = parse_syndication_payload("{not json", "123");
  ASSERT_FALSE(media.has_value());
}

TEST(SyndicationPayload, PicksHighestBitrateMp4AndIgnoresHls) {
  const std::string json = R"({
    "mediaDetails": [{
      "type": "video",
      "video_info": {
        "duration_millis": 5000,
        "variants": [
          {"content_type": "application/x-mpegURL", "url": "https://cdn/playlist.m3u8"},
          {"content_type": "video/mp4", "bitrate": 832000,  "url": "https://cdn/low.mp4"},
          {"content_type": "video/mp4", "bitrate": 2176000, "url": "https://cdn/high.mp4"},
          {"content_type": "video/mp4", "bitrate": 1280000, "url": "https://cdn/mid.mp4"}
        ]
      }
    }]
  })";
  auto media = parse_syndication_payload(json, "1");
  ASSERT_TRUE(media.has_value()) << media.error().message;
  ASSERT_EQ(media->size(), 1u);
  EXPECT_EQ((*media)[0].url, "https://cdn/high.mp4");
}

TEST(SyndicationPayload, StripsQueryStringFromMediaUrl) {
  const std::string json = R"({
    "mediaDetails": [{
      "type": "animated_gif",
      "video_info": {"variants": [
        {"content_type": "video/mp4", "bitrate": 0, "url": "https://cdn/a.mp4?tag=12"}
      ]}
    }]
  })";
  auto media = parse_syndication_payload(json, "1");
  ASSERT_TRUE(media.has_value());
  ASSERT_EQ(media->size(), 1u);
  EXPECT_EQ((*media)[0].url, "https://cdn/a.mp4");
}

TEST(SyndicationPayload, HandlesTweetsWithSeveralMediaItems) {
  const std::string json = R"({
    "mediaDetails": [
      {"type": "photo"},
      {"type": "video", "video_info": {"duration_millis": 1000, "variants": [
        {"content_type": "video/mp4", "bitrate": 100, "url": "https://cdn/one.mp4"}]}},
      {"type": "animated_gif", "video_info": {"variants": [
        {"content_type": "video/mp4", "bitrate": 200, "url": "https://cdn/two.mp4"}]}}
    ]
  })";
  auto media = parse_syndication_payload(json, "1");
  ASSERT_TRUE(media.has_value());
  ASSERT_EQ(media->size(), 2u);
  EXPECT_EQ((*media)[0].kind, MediaKind::Video);
  EXPECT_EQ((*media)[1].kind, MediaKind::Gif);
}

TEST(SyndicationUrl, IncludesIdAndComputedToken) {
  const auto url = xdl::build_syndication_url("1491475671058681863");
  EXPECT_NE(url.find("id=1491475671058681863"), std::string::npos);
  EXPECT_NE(url.find("token=3m5lxayrcgmflrnsfko6r"), std::string::npos);
  EXPECT_TRUE(url.starts_with("https://cdn.syndication.twimg.com/tweet-result"));
}

TEST(SyndicationSource, MapsHttp404ToUnavailable) {
  FakeHttpClient http;
  http.on_get("tweet-result", xdl::HttpResponse{404, ""});

  xdl::SyndicationSource source{http};
  auto media = source.fetch("123", std::chrono::milliseconds{1000});
  ASSERT_FALSE(media.has_value());
  EXPECT_EQ(media.error().kind, Error::Kind::Unavailable);
}

TEST(SyndicationSource, MapsOtherFailureStatusesToNetwork) {
  FakeHttpClient http;
  http.on_get("tweet-result", xdl::HttpResponse{503, ""});

  xdl::SyndicationSource source{http};
  auto media = source.fetch("123", std::chrono::milliseconds{1000});
  ASSERT_FALSE(media.has_value());
  EXPECT_EQ(media.error().kind, Error::Kind::Network);
}

TEST(SyndicationSource, SendsTheTokenisedUrl) {
  FakeHttpClient http;
  http.on_get("tweet-result", xdl::HttpResponse{200, load_fixture("video.json")});

  xdl::SyndicationSource source{http};
  auto media = source.fetch("1491475671058681863", std::chrono::milliseconds{1000});
  ASSERT_TRUE(media.has_value()) << media.error().message;
  ASSERT_EQ(http.requested_urls.size(), 1u);
  EXPECT_NE(http.requested_urls[0].find("token="), std::string::npos);
}

}  // namespace
