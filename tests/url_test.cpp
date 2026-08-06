#include "xdl/url.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using xdl::extract_tweet_id;
using xdl::normalize_url;

TEST(NormalizeUrl, StripsTheShareParameter) {
  EXPECT_EQ(normalize_url("https://x.com/found_it_funny/status/2085077475109769243?s=20"),
            "https://x.com/found_it_funny/status/2085077475109769243");
}

TEST(NormalizeUrl, StripsEveryTrackingParameter) {
  EXPECT_EQ(normalize_url("https://twitter.com/a/status/123?s=20&t=abc123&ref_src=twsrc%5Etfw"),
            "https://twitter.com/a/status/123");
}

TEST(NormalizeUrl, DropsFragments) {
  EXPECT_EQ(normalize_url("https://x.com/a/status/123/video/1?s=46#anchor"),
            "https://x.com/a/status/123/video/1");
}

TEST(NormalizeUrl, PreservesNonTrackingParameters) {
  EXPECT_EQ(normalize_url("https://x.com/a/status/123?lang=en&s=20"),
            "https://x.com/a/status/123?lang=en");
}

TEST(NormalizeUrl, PreservesParameterOrderAndSeparators) {
  EXPECT_EQ(normalize_url("https://x.com/a/status/123?lang=en&s=20&foo=bar"),
            "https://x.com/a/status/123?lang=en&foo=bar");
}

TEST(NormalizeUrl, LeavesNonUrlInputAlone) {
  EXPECT_EQ(normalize_url("2085077475109769243"), "2085077475109769243");
  EXPECT_EQ(normalize_url("# a comment"), "# a comment");
}

TEST(NormalizeUrl, TrimsSurroundingWhitespace) {
  EXPECT_EQ(normalize_url("  https://x.com/a/status/123?s=20\n"),
            "https://x.com/a/status/123");
}

TEST(NormalizeUrl, LeavesUrlWithoutQueryUntouched) {
  EXPECT_EQ(normalize_url("https://x.com/a/status/123"), "https://x.com/a/status/123");
}

TEST(ExtractTweetId, PullsIdThroughTrackingParameters) {
  auto id = extract_tweet_id("https://x.com/found_it_funny/status/2085077475109769243?s=20");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(*id, "2085077475109769243");
}

TEST(ExtractTweetId, AcceptsBareIds) {
  auto id = extract_tweet_id("2085077475109769243");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(*id, "2085077475109769243");
}

TEST(ExtractTweetId, HandlesPhotoAndVideoSuffixes) {
  auto photo = extract_tweet_id("https://x.com/a/status/123456/photo/1");
  ASSERT_TRUE(photo.has_value());
  EXPECT_EQ(*photo, "123456");

  auto video = extract_tweet_id("https://x.com/a/status/123456/video/1?s=46");
  ASSERT_TRUE(video.has_value());
  EXPECT_EQ(*video, "123456");
}

TEST(ExtractTweetId, AcceptsMirrorDomains) {
  for (const auto* url : {"https://fxtwitter.com/a/status/999888777",
                          "https://vxtwitter.com/a/status/999888777",
                          "https://fixupx.com/a/status/999888777",
                          "https://mobile.twitter.com/a/status/999888777"}) {
    auto id = extract_tweet_id(url);
    ASSERT_TRUE(id.has_value()) << url;
    EXPECT_EQ(*id, "999888777") << url;
  }
}

TEST(ExtractTweetId, AcceptsStatusesSpelling) {
  auto id = extract_tweet_id("https://twitter.com/a/statuses/424242424");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(*id, "424242424");
}

TEST(ExtractTweetId, RejectsEmptyInput) {
  auto id = extract_tweet_id("   ");
  ASSERT_FALSE(id.has_value());
  EXPECT_EQ(id.error().kind, xdl::Error::Kind::BadInput);
  EXPECT_EQ(id.error().message, "empty input");
}

TEST(ExtractTweetId, RejectsUnrelatedUrls) {
  auto id = extract_tweet_id("https://example.com/not/a/tweet");
  ASSERT_FALSE(id.has_value());
  EXPECT_EQ(id.error().kind, xdl::Error::Kind::BadInput);
}

}  // namespace
