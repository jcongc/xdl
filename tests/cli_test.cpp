#include "xdl/cli.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using xdl::BackendChoice;
using xdl::Error;
using xdl::OnlyFilter;
using xdl::parse_args;

xdl::Result<xdl::ParsedArgs> parse(std::vector<std::string> args) {
  return parse_args(args);
}

// Every default here is pinned against the Python tool, so muscle memory and
// existing scripts carry over unchanged.
TEST(ParseArgs, DefaultsMatchThePythonTool) {
  auto parsed = parse({"https://x.com/a/status/1"});
  ASSERT_TRUE(parsed.has_value()) << parsed.error().message;

  const auto& o = parsed->options;
  EXPECT_EQ(o.only, OnlyFilter::All);
  EXPECT_FALSE(o.make_gif);
  EXPECT_TRUE(o.keep_mp4);
  EXPECT_EQ(o.fps, 15);
  EXPECT_EQ(o.width, 480);
  EXPECT_EQ(o.start, 0.0);
  EXPECT_FALSE(o.duration.has_value());
  EXPECT_EQ(o.backend, BackendChoice::Auto);
  EXPECT_EQ(o.jobs, 4u);
  EXPECT_FALSE(o.overwrite);
  EXPECT_EQ(o.timeout, std::chrono::milliseconds{30000});
  EXPECT_EQ(o.outdir.filename(), "Downloads");
}

TEST(ParseArgs, CollectsPositionalSources) {
  auto parsed = parse({"111", "https://x.com/a/status/222", "-"});
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQ(parsed->sources.size(), 3u);
  EXPECT_EQ(parsed->sources[0], "111");
  EXPECT_EQ(parsed->sources[2], "-");
}

TEST(ParseArgs, AcceptsShortAndLongJobsFlags) {
  auto short_form = parse({"-j", "8", "1"});
  auto long_form = parse({"--jobs", "8", "1"});
  ASSERT_TRUE(short_form.has_value());
  ASSERT_TRUE(long_form.has_value());
  EXPECT_EQ(short_form->options.jobs, 8u);
  EXPECT_EQ(long_form->options.jobs, 8u);
}

TEST(ParseArgs, ParsesGifRelatedFlags) {
  auto parsed = parse({"--gif", "--no-keep-mp4", "--fps", "20", "--width", "-1",
                       "--start", "2.5", "--duration", "3", "1"});
  ASSERT_TRUE(parsed.has_value()) << parsed.error().message;

  const auto& o = parsed->options;
  EXPECT_TRUE(o.make_gif);
  EXPECT_FALSE(o.keep_mp4);
  EXPECT_EQ(o.fps, 20);
  EXPECT_EQ(o.width, -1);
  EXPECT_EQ(o.start, 2.5);
  ASSERT_TRUE(o.duration.has_value());
  EXPECT_EQ(*o.duration, 3.0);
}

TEST(ParseArgs, ParsesEveryOnlyValue) {
  EXPECT_EQ(parse({"--only", "all", "1"})->options.only, OnlyFilter::All);
  EXPECT_EQ(parse({"--only", "gif", "1"})->options.only, OnlyFilter::Gif);
  EXPECT_EQ(parse({"--only", "video", "1"})->options.only, OnlyFilter::Video);
}

TEST(ParseArgs, ParsesEveryBackendValue) {
  EXPECT_EQ(parse({"--backend", "auto", "1"})->options.backend, BackendChoice::Auto);
  EXPECT_EQ(parse({"--backend", "syndication", "1"})->options.backend,
            BackendChoice::Syndication);
  EXPECT_EQ(parse({"--backend", "yt-dlp", "1"})->options.backend, BackendChoice::YtDlp);
}

TEST(ParseArgs, ConvertsTimeoutSecondsToMilliseconds) {
  auto parsed = parse({"--timeout", "2.5", "1"});
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->options.timeout, std::chrono::milliseconds{2500});
}

TEST(ParseArgs, RejectsUnknownOnlyValues) {
  auto parsed = parse({"--only", "bogus", "1"});
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error().kind, Error::Kind::BadInput);
  EXPECT_NE(parsed.error().message.find("--only"), std::string::npos);
}

TEST(ParseArgs, RejectsUnknownBackendValues) {
  auto parsed = parse({"--backend", "carrier-pigeon", "1"});
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error().kind, Error::Kind::BadInput);
}

TEST(ParseArgs, RejectsFlagsMissingTheirValue) {
  for (const char* flag : {"--fps", "--width", "--only", "--timeout", "-o", "-j"}) {
    auto parsed = parse({flag});
    ASSERT_FALSE(parsed.has_value()) << flag;
    EXPECT_EQ(parsed.error().kind, Error::Kind::BadInput) << flag;
  }
}

TEST(ParseArgs, RejectsNonNumericNumbers) {
  auto parsed = parse({"--fps", "fast", "1"});
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error().kind, Error::Kind::BadInput);
}

TEST(ParseArgs, RejectsTrailingGarbageInNumbers) {
  auto parsed = parse({"--fps", "15abc", "1"});
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error().kind, Error::Kind::BadInput);
}

TEST(ParseArgs, RejectsZeroJobs) {
  auto parsed = parse({"-j", "0", "1"});
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error().kind, Error::Kind::BadInput);
}

TEST(ParseArgs, RejectsUnrecognisedFlags) {
  auto parsed = parse({"--turbo", "1"});
  ASSERT_FALSE(parsed.has_value());
  EXPECT_NE(parsed.error().message.find("--turbo"), std::string::npos);
}

TEST(ParseArgs, TreatsBareDashAsASourceNotAFlag) {
  auto parsed = parse({"-"});
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQ(parsed->sources.size(), 1u);
  EXPECT_EQ(parsed->sources[0], "-");
}

TEST(ParseArgs, RequestsHelp) {
  EXPECT_TRUE(parse({"--help"})->help);
  EXPECT_TRUE(parse({"-h"})->help);
}

TEST(ParseArgs, ReportsUnreadableSourceFiles) {
  auto parsed = parse({"--from-file", "/nonexistent/path/urls.txt"});
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error().kind, Error::Kind::Io);
}

TEST(Usage, MentionsEveryFlag) {
  const auto text = xdl::usage();
  for (const char* flag : {"--from-file", "--only", "--gif", "--no-keep-mp4",
                           "--fps", "--width", "--start", "--duration",
                           "--backend", "--jobs", "--overwrite", "--timeout"}) {
    EXPECT_NE(text.find(flag), std::string::npos) << flag;
  }
}

}  // namespace
