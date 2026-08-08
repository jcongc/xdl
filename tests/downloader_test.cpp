#include "xdl/downloader.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fakes/fake_http.hpp"
#include "fakes/fake_process.hpp"
#include "fixture_loader.hpp"

namespace {

using xdl::Downloader;
using xdl::Options;
using xdl::testing::FakeHttpClient;
using xdl::testing::FakeProcessRunner;
using xdl::testing::load_fixture;

// Each test gets its own directory under the system temp dir, removed on exit.
class DownloaderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = std::filesystem::temp_directory_path() /
           ("xdl-test-" + std::string{info->name()});
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);

    options_.outdir = dir_;
    options_.jobs = 2;
    // Backend is pinned so tests never depend on yt-dlp being installed.
    options_.backend = xdl::BackendChoice::Syndication;
  }

  void TearDown() override { std::filesystem::remove_all(dir_); }

  void serve(const std::string& fixture) {
    http_.on_get("tweet-result", xdl::HttpResponse{200, load_fixture(fixture)});
  }

  void serve_json(const std::string& json) {
    http_.on_get("tweet-result", xdl::HttpResponse{200, json});
  }

  std::filesystem::path dir_;
  FakeHttpClient http_;
  FakeProcessRunner runner_;
  Options options_;
};

TEST_F(DownloaderTest, NamesSingleMediaAfterTheTweetId) {
  serve("video.json");
  Downloader downloader{http_, runner_, options_};

  const auto result = downloader.process_one(
      "https://x.com/NASA/status/1491475671058681863?s=20");

  ASSERT_FALSE(result.error.has_value()) << result.error->message;
  ASSERT_EQ(result.files.size(), 1u);
  EXPECT_EQ(result.files[0].filename(), "1491475671058681863.mp4");
  EXPECT_TRUE(std::filesystem::exists(result.files[0]));
}

TEST_F(DownloaderTest, SuffixesFilenamesWhenATweetCarriesSeveralMedia) {
  serve_json(R"({"mediaDetails": [
    {"type": "video", "video_info": {"variants": [
      {"content_type": "video/mp4", "bitrate": 1, "url": "https://cdn/a.mp4"}]}},
    {"type": "video", "video_info": {"variants": [
      {"content_type": "video/mp4", "bitrate": 1, "url": "https://cdn/b.mp4"}]}}
  ]})");
  Downloader downloader{http_, runner_, options_};

  const auto result = downloader.process_one("https://x.com/a/status/777");

  ASSERT_FALSE(result.error.has_value()) << result.error->message;
  ASSERT_EQ(result.files.size(), 2u);
  EXPECT_EQ(result.files[0].filename(), "777_1.mp4");
  EXPECT_EQ(result.files[1].filename(), "777_2.mp4");
}

TEST_F(DownloaderTest, NormalisesTheReportedSource) {
  serve("video.json");
  Downloader downloader{http_, runner_, options_};

  const auto result = downloader.process_one(
      "https://x.com/NASA/status/1491475671058681863?s=20&t=abc");

  EXPECT_EQ(result.source, "https://x.com/NASA/status/1491475671058681863");
}

TEST_F(DownloaderTest, SkipsDownloadWhenTheTargetAlreadyExists) {
  serve("video.json");
  {
    std::ofstream existing{dir_ / "1491475671058681863.mp4"};
    existing << "already here";
  }
  Downloader downloader{http_, runner_, options_};

  const auto result = downloader.process_one("1491475671058681863");

  ASSERT_FALSE(result.error.has_value()) << result.error->message;
  EXPECT_TRUE(http_.downloaded.empty()) << "existing file should not be re-fetched";
  ASSERT_EQ(result.files.size(), 1u);
}

TEST_F(DownloaderTest, OverwriteForcesAReDownload) {
  serve("video.json");
  {
    std::ofstream existing{dir_ / "1491475671058681863.mp4"};
    existing << "stale";
  }
  options_.overwrite = true;
  Downloader downloader{http_, runner_, options_};

  const auto result = downloader.process_one("1491475671058681863");

  ASSERT_FALSE(result.error.has_value()) << result.error->message;
  EXPECT_EQ(http_.downloaded.size(), 1u);
}

TEST_F(DownloaderTest, GifConversionRunsExactlyTwoFfmpegPasses) {
  serve("animated_gif.json");
  options_.make_gif = true;
  Downloader downloader{http_, runner_, options_};

  const auto result = downloader.process_one("746487912313688067");

  if (!xdl::program_exists("ffmpeg")) {
    GTEST_SKIP() << "ffmpeg not installed";
  }
  ASSERT_FALSE(result.error.has_value()) << result.error->message;
  ASSERT_EQ(runner_.calls.size(), 2u) << "palettegen then paletteuse";
  EXPECT_NE(std::find(runner_.calls[0].begin(), runner_.calls[0].end(), "-vf"),
            runner_.calls[0].end());
  EXPECT_NE(std::find(runner_.calls[1].begin(), runner_.calls[1].end(), "-lavfi"),
            runner_.calls[1].end());
}

TEST_F(DownloaderTest, KeepsTheMp4AlongsideTheGifByDefault) {
  serve("animated_gif.json");
  options_.make_gif = true;
  Downloader downloader{http_, runner_, options_};

  const auto result = downloader.process_one("746487912313688067");

  if (!xdl::program_exists("ffmpeg")) {
    GTEST_SKIP() << "ffmpeg not installed";
  }
  ASSERT_FALSE(result.error.has_value()) << result.error->message;
  ASSERT_EQ(result.files.size(), 2u);
  EXPECT_EQ(result.files[0].extension(), ".gif");
  EXPECT_EQ(result.files[1].extension(), ".mp4");
}

TEST_F(DownloaderTest, NoKeepMp4DeletesTheIntermediateFile) {
  serve("animated_gif.json");
  options_.make_gif = true;
  options_.keep_mp4 = false;
  Downloader downloader{http_, runner_, options_};

  const auto result = downloader.process_one("746487912313688067");

  if (!xdl::program_exists("ffmpeg")) {
    GTEST_SKIP() << "ffmpeg not installed";
  }
  ASSERT_FALSE(result.error.has_value()) << result.error->message;
  ASSERT_EQ(result.files.size(), 1u);
  EXPECT_EQ(result.files[0].extension(), ".gif");
  EXPECT_FALSE(std::filesystem::exists(dir_ / "746487912313688067.mp4"));
}

TEST_F(DownloaderTest, WarnsWhenConvertingALongClip) {
  serve("video.json");  // 204.9 seconds
  options_.make_gif = true;
  Downloader downloader{http_, runner_, options_};

  const auto result = downloader.process_one("1491475671058681863");

  if (!xdl::program_exists("ffmpeg")) {
    GTEST_SKIP() << "ffmpeg not installed";
  }
  ASSERT_EQ(result.notes.size(), 1u);
  EXPECT_NE(result.notes[0].find("205s"), std::string::npos);
}

TEST_F(DownloaderTest, StaysQuietWhenTheClipIsTrimmedShort) {
  serve("video.json");
  options_.make_gif = true;
  options_.duration = 3.0;
  Downloader downloader{http_, runner_, options_};

  const auto result = downloader.process_one("1491475671058681863");

  if (!xdl::program_exists("ffmpeg")) {
    GTEST_SKIP() << "ffmpeg not installed";
  }
  EXPECT_TRUE(result.notes.empty());
}

TEST_F(DownloaderTest, ReportsPhotoOnlyTweetsAsFailures) {
  serve("photo_only.json");
  Downloader downloader{http_, runner_, options_};

  const auto result = downloader.process_one(
      "https://x.com/found_it_funny/status/2085077475109769243?s=20");

  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(result.error->kind, xdl::Error::Kind::NoMedia);
  EXPECT_TRUE(result.files.empty());
}

TEST_F(DownloaderTest, RejectsInputWithNoTweetId) {
  Downloader downloader{http_, runner_, options_};
  const auto result = downloader.process_one("https://example.com/nope");

  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(result.error->kind, xdl::Error::Kind::BadInput);
}

TEST_F(DownloaderTest, OnlyFilterExcludesTheWrongKind) {
  serve("video.json");
  options_.only = xdl::OnlyFilter::Gif;
  Downloader downloader{http_, runner_, options_};

  const auto result = downloader.process_one("1491475671058681863");

  ASSERT_TRUE(result.error.has_value());
  EXPECT_NE(result.error->message.find("no animated GIF"), std::string::npos);
}

TEST_F(DownloaderTest, BatchRunPreservesInputOrderAndSurvivesFailures) {
  serve("video.json");
  Downloader downloader{http_, runner_, options_};

  const std::vector<std::string> sources{
      "https://x.com/a/status/1491475671058681863",
      "https://example.com/not-a-tweet",
      "https://x.com/a/status/1491475671058681863?s=20",
  };
  const auto results = downloader.run(sources);

  ASSERT_EQ(results.size(), 3u);
  EXPECT_FALSE(results[0].error.has_value());
  EXPECT_TRUE(results[1].error.has_value()) << "one bad source must not abort the batch";
  EXPECT_FALSE(results[2].error.has_value());
}

// Regression: main once shared a single CurlHttpClient — and therefore a
// single CURL* easy handle — across every pool worker. libcurl forbids
// concurrent use of one easy handle, and the result was an intermittent
// SIGABRT that only appeared with several sources and -j > 1. This drives many
// distinct tweets through a real ThreadPool so the sanitiser builds have
// something to catch if shared state creeps back in.
TEST_F(DownloaderTest, HandlesManySourcesConcurrentlyWithoutRaces) {
  serve_json(R"({"mediaDetails": [{"type": "video", "video_info": {
    "duration_millis": 1000,
    "variants": [{"content_type": "video/mp4", "bitrate": 1, "url": "https://cdn/a.mp4"}]}}]})");
  options_.jobs = 8;
  Downloader downloader{http_, runner_, options_};

  std::vector<std::string> sources;
  for (int i = 0; i < 64; ++i) {
    sources.push_back("https://x.com/a/status/" + std::to_string(100000 + i));
  }

  const auto results = downloader.run(sources);

  ASSERT_EQ(results.size(), 64u);
  for (size_t i = 0; i < results.size(); ++i) {
    ASSERT_FALSE(results[i].error.has_value())
        << "source " << i << ": " << results[i].error->message;
    ASSERT_EQ(results[i].files.size(), 1u);
    // Order must survive the fan-out.
    EXPECT_EQ(results[i].files[0].filename().string(),
              std::to_string(100000 + i) + ".mp4");
  }
  EXPECT_EQ(http_.downloaded.size(), 64u);
}

TEST(CollectSources, DeduplicatesEquivalentSpellings) {
  const std::vector<std::string> raw{
      "https://x.com/a/status/123?s=20",
      "https://x.com/a/status/123?t=abc&s=46",
      "https://x.com/a/status/123",
  };
  const auto sources = xdl::collect_sources(raw);
  ASSERT_EQ(sources.size(), 1u);
  EXPECT_EQ(sources[0], "https://x.com/a/status/123");
}

TEST(CollectSources, SkipsBlanksAndComments) {
  const std::vector<std::string> raw{
      "", "   ", "# a comment", "https://x.com/a/status/1", "\t\n",
  };
  const auto sources = xdl::collect_sources(raw);
  ASSERT_EQ(sources.size(), 1u);
  EXPECT_EQ(sources[0], "https://x.com/a/status/1");
}

TEST(CollectSources, PreservesFirstSeenOrder) {
  const std::vector<std::string> raw{
      "https://x.com/a/status/3",
      "https://x.com/a/status/1",
      "https://x.com/a/status/2",
      "https://x.com/a/status/1?s=20",
  };
  const auto sources = xdl::collect_sources(raw);
  ASSERT_EQ(sources.size(), 3u);
  EXPECT_EQ(sources[0], "https://x.com/a/status/3");
  EXPECT_EQ(sources[1], "https://x.com/a/status/1");
  EXPECT_EQ(sources[2], "https://x.com/a/status/2");
}

}  // namespace
