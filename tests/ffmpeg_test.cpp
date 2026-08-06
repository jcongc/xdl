#include "xdl/ffmpeg.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace {

using xdl::build_filter_chain;
using xdl::build_palettegen_argv;
using xdl::build_paletteuse_argv;
using xdl::GifSpec;

std::ptrdiff_t index_of(const std::vector<std::string>& argv, const std::string& needle) {
  const auto it = std::find(argv.begin(), argv.end(), needle);
  return it == argv.end() ? -1 : std::distance(argv.begin(), it);
}

std::ptrdiff_t last_index_of(const std::vector<std::string>& argv, const std::string& needle) {
  const auto it = std::find(argv.rbegin(), argv.rend(), needle);
  return it == argv.rend() ? -1 : std::distance(argv.begin(), it.base()) - 1;
}

bool contains(const std::vector<std::string>& argv, const std::string& needle) {
  return std::find(argv.begin(), argv.end(), needle) != argv.end();
}

// Regression: when -t sits between the two -i flags of the paletteuse pass,
// ffmpeg reads it as an input option for the palette PNG and encodes the whole
// clip. That turned a 3-second request into a 46 MB GIF.
TEST(BuildGifArgv, DurationFlagFollowsTheLastInput) {
  GifSpec spec;
  spec.duration = 3.0;
  const auto argv = build_paletteuse_argv("in.mp4", "palette.png", "out.gif", spec);

  const auto last_input = last_index_of(argv, "-i");
  const auto duration = index_of(argv, "-t");

  ASSERT_GE(last_input, 0);
  ASSERT_GE(duration, 0);
  EXPECT_GT(duration, last_input)
      << "-t before the last -i is parsed as an input option for the palette";
}

TEST(BuildGifArgv, PalettegenAlsoPlacesDurationAfterItsInput) {
  GifSpec spec;
  spec.duration = 3.0;
  const auto argv = build_palettegen_argv("in.mp4", "palette.png", spec);
  EXPECT_GT(index_of(argv, "-t"), last_index_of(argv, "-i"));
}

TEST(BuildGifArgv, StartFlagPrecedesTheFirstInput) {
  GifSpec spec;
  spec.start = 5.0;
  const auto argv = build_paletteuse_argv("in.mp4", "palette.png", "out.gif", spec);
  const auto start = index_of(argv, "-ss");
  ASSERT_GE(start, 0);
  EXPECT_LT(start, index_of(argv, "-i"));
}

TEST(BuildGifArgv, OmitsTrimFlagsWhenNotRequested) {
  const GifSpec spec;
  const auto argv = build_paletteuse_argv("in.mp4", "palette.png", "out.gif", spec);
  EXPECT_FALSE(contains(argv, "-ss"));
  EXPECT_FALSE(contains(argv, "-t"));
}

TEST(BuildGifArgv, BothPassesShareIdenticalTrimFlags) {
  GifSpec spec;
  spec.start = 2.5;
  spec.duration = 4.0;
  const auto gen = build_palettegen_argv("in.mp4", "palette.png", spec);
  const auto use = build_paletteuse_argv("in.mp4", "palette.png", "out.gif", spec);

  ASSERT_GE(index_of(gen, "-ss"), 0);
  ASSERT_GE(index_of(use, "-ss"), 0);
  EXPECT_EQ(gen[static_cast<size_t>(index_of(gen, "-ss")) + 1],
            use[static_cast<size_t>(index_of(use, "-ss")) + 1]);
  EXPECT_EQ(gen[static_cast<size_t>(index_of(gen, "-t")) + 1],
            use[static_cast<size_t>(index_of(use, "-t")) + 1]);
}

// Regression: the help text promised width -1 keeps the source size, but the
// generated filter was scale=-1:-1, which ffmpeg rejects outright.
TEST(BuildFilterChain, DropsScaleFilterWhenWidthIsNotPositive) {
  GifSpec spec;
  spec.width = -1;
  spec.fps = 20;
  EXPECT_EQ(build_filter_chain(spec), "fps=20");

  spec.width = 0;
  EXPECT_EQ(build_filter_chain(spec), "fps=20");
}

TEST(BuildFilterChain, IncludesScaleFilterForPositiveWidth) {
  GifSpec spec;
  spec.width = 480;
  spec.fps = 15;
  EXPECT_EQ(build_filter_chain(spec), "fps=15,scale=480:-1:flags=lanczos");
}

TEST(BuildGifArgv, FormatsWholeSecondsWithoutTrailingZeros) {
  GifSpec spec;
  spec.start = 5.0;
  spec.duration = 3.0;
  const auto argv = build_paletteuse_argv("in.mp4", "palette.png", "out.gif", spec);
  EXPECT_EQ(argv[static_cast<size_t>(index_of(argv, "-ss")) + 1], "5");
  EXPECT_EQ(argv[static_cast<size_t>(index_of(argv, "-t")) + 1], "3");
}

TEST(BuildGifArgv, KeepsFractionalSeconds) {
  GifSpec spec;
  spec.start = 1.25;
  const auto argv = build_paletteuse_argv("in.mp4", "palette.png", "out.gif", spec);
  EXPECT_EQ(argv[static_cast<size_t>(index_of(argv, "-ss")) + 1], "1.25");
}

TEST(BuildGifArgv, LoopsForever) {
  const GifSpec spec;
  const auto argv = build_paletteuse_argv("in.mp4", "palette.png", "out.gif", spec);
  const auto loop = index_of(argv, "-loop");
  ASSERT_GE(loop, 0);
  EXPECT_EQ(argv[static_cast<size_t>(loop) + 1], "0");
}

TEST(BuildGifArgv, PutsDestinationLast) {
  const GifSpec spec;
  const auto argv = build_paletteuse_argv("in.mp4", "palette.png", "out.gif", spec);
  EXPECT_EQ(argv.back(), "out.gif");
  EXPECT_EQ(argv.front(), "ffmpeg");
}

TEST(BuildGifArgv, PalettegenWritesThePaletteLast) {
  const GifSpec spec;
  const auto argv = build_palettegen_argv("in.mp4", "palette.png", spec);
  EXPECT_EQ(argv.back(), "palette.png");
}

}  // namespace
