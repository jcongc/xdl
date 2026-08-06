#include "xdl/backend.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

using xdl::Error;
using xdl::Media;
using xdl::MediaKind;
using xdl::MediaSource;
using xdl::OnlyFilter;
using xdl::resolve_media;

// A source whose behaviour each test dictates outright.
class ScriptedSource final : public MediaSource {
 public:
  ScriptedSource(std::string name, xdl::Result<std::vector<Media>> outcome)
      : name_(std::move(name)), outcome_(std::move(outcome)) {}

  std::string_view name() const override { return name_; }

  xdl::Result<std::vector<Media>> fetch(std::string_view, std::chrono::milliseconds) override {
    ++calls;
    if (throws) {
      throw std::runtime_error{"scripted explosion"};
    }
    return outcome_;
  }

  int calls{0};
  bool throws{false};

 private:
  std::string name_;
  xdl::Result<std::vector<Media>> outcome_;
};

Media gif(std::string url = "https://cdn/g.mp4") {
  return Media{std::move(url), MediaKind::Gif, 0};
}
Media video(std::string url = "https://cdn/v.mp4", int ms = 5000) {
  return Media{std::move(url), MediaKind::Video, ms};
}

std::chrono::milliseconds timeout() { return std::chrono::milliseconds{1000}; }

TEST(FilterMedia, AllKeepsEverything) {
  const std::vector<Media> media{gif(), video()};
  EXPECT_EQ(xdl::filter_media(media, OnlyFilter::All).size(), 2u);
}

TEST(FilterMedia, NarrowsToRequestedKind) {
  const std::vector<Media> media{gif(), video()};
  const auto gifs = xdl::filter_media(media, OnlyFilter::Gif);
  ASSERT_EQ(gifs.size(), 1u);
  EXPECT_EQ(gifs[0].kind, MediaKind::Gif);

  const auto videos = xdl::filter_media(media, OnlyFilter::Video);
  ASSERT_EQ(videos.size(), 1u);
  EXPECT_EQ(videos[0].kind, MediaKind::Video);
}

TEST(ResolveMedia, FirstSuccessfulSourceShortCircuits) {
  ScriptedSource first{"syndication", std::vector<Media>{video()}};
  ScriptedSource second{"yt-dlp", std::vector<Media>{gif()}};
  MediaSource* sources[] = {&first, &second};

  auto media = resolve_media(sources, "1", OnlyFilter::All, timeout());
  ASSERT_TRUE(media.has_value());
  EXPECT_EQ(media->size(), 1u);
  EXPECT_EQ(first.calls, 1);
  EXPECT_EQ(second.calls, 0) << "second backend should not run after the first succeeds";
}

TEST(ResolveMedia, FallsThroughWhenFirstSourceFails) {
  ScriptedSource first{"syndication",
                       std::unexpected(Error{Error::Kind::Network, "boom"})};
  ScriptedSource second{"yt-dlp", std::vector<Media>{gif()}};
  MediaSource* sources[] = {&first, &second};

  auto media = resolve_media(sources, "1", OnlyFilter::All, timeout());
  ASSERT_TRUE(media.has_value());
  EXPECT_EQ(second.calls, 1);
}

TEST(ResolveMedia, FallsThroughWhenFirstSourceHasNothingMatchingTheFilter) {
  ScriptedSource first{"syndication", std::vector<Media>{video()}};
  ScriptedSource second{"yt-dlp", std::vector<Media>{gif()}};
  MediaSource* sources[] = {&first, &second};

  auto media = resolve_media(sources, "1", OnlyFilter::Gif, timeout());
  ASSERT_TRUE(media.has_value());
  ASSERT_EQ(media->size(), 1u);
  EXPECT_EQ((*media)[0].kind, MediaKind::Gif);
  EXPECT_EQ(second.calls, 1);
}

TEST(ResolveMedia, JoinsEveryFailureReason) {
  ScriptedSource first{"syndication",
                       std::unexpected(Error{Error::Kind::Network, "HTTP 503"})};
  ScriptedSource second{"yt-dlp",
                        std::unexpected(Error{Error::Kind::Subprocess, "not installed"})};
  MediaSource* sources[] = {&first, &second};

  auto media = resolve_media(sources, "1", OnlyFilter::All, timeout());
  ASSERT_FALSE(media.has_value());
  EXPECT_EQ(media.error().message, "syndication: HTTP 503; yt-dlp: not installed");
}

TEST(ResolveMedia, DescribesTheRequestedKindWhenNothingMatches) {
  ScriptedSource only{"syndication", std::vector<Media>{video()}};
  MediaSource* sources[] = {&only};

  auto media = resolve_media(sources, "1", OnlyFilter::Gif, timeout());
  ASSERT_FALSE(media.has_value());
  EXPECT_EQ(media.error().kind, Error::Kind::NoMedia);
  EXPECT_EQ(media.error().message, "syndication: no animated GIF in tweet");
}

TEST(ResolveMedia, ReportsEmptyTweetsUsingTheAllWording) {
  ScriptedSource only{"syndication", std::vector<Media>{}};
  MediaSource* sources[] = {&only};

  auto media = resolve_media(sources, "1", OnlyFilter::All, timeout());
  ASSERT_FALSE(media.has_value());
  EXPECT_EQ(media.error().message, "syndication: no video or GIF in tweet");
}

// One misbehaving backend must not take down the whole batch.
TEST(ResolveMedia, ContainsExceptionsThrownBySources) {
  ScriptedSource first{"syndication", std::vector<Media>{}};
  first.throws = true;
  ScriptedSource second{"yt-dlp", std::vector<Media>{gif()}};
  MediaSource* sources[] = {&first, &second};

  auto media = resolve_media(sources, "1", OnlyFilter::All, timeout());
  ASSERT_TRUE(media.has_value());
  EXPECT_EQ(second.calls, 1);
}

TEST(ResolveMedia, ReportsWhenThereAreNoSourcesAtAll) {
  std::span<MediaSource* const> none;
  auto media = resolve_media(none, "1", OnlyFilter::All, timeout());
  ASSERT_FALSE(media.has_value());
  EXPECT_EQ(media.error().kind, Error::Kind::NoMedia);
}

}  // namespace
