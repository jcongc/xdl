#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "xdl/error.hpp"

namespace xdl {

class ProcessRunner;

struct GifSpec {
  int fps{15};
  int width{480};  // <= 0 keeps the source width
  double start{0.0};
  std::optional<double> duration;
};

// Building the ffmpeg command line is kept separate from running it so the
// argument *order* is unit-testable. Two ordering rules are load-bearing:
//
//   1. -t must come after the LAST -i. In the paletteuse pass there are two
//      inputs; a -t between them is parsed as an input option for the palette
//      and the whole clip gets encoded instead of the requested slice.
//   2. When width <= 0 the scale filter must be omitted entirely, because
//      scale=-1:-1 is not a valid filter expression.
//
// Both passes must receive identical trim flags, so the palette is generated
// from exactly the frames it will be applied to.
std::vector<std::string> build_palettegen_argv(const std::filesystem::path& src,
                                               const std::filesystem::path& palette,
                                               const GifSpec& spec);

std::vector<std::string> build_paletteuse_argv(const std::filesystem::path& src,
                                               const std::filesystem::path& palette,
                                               const std::filesystem::path& dest,
                                               const GifSpec& spec);

// The shared filter chain ("fps=15,scale=480:-1:flags=lanczos").
std::string build_filter_chain(const GifSpec& spec);

// Runs the two passes against a temporary palette, cleaned up on every exit
// path. Requires ffmpeg on PATH.
Result<void> encode_gif(ProcessRunner& runner,
                        const std::filesystem::path& src,
                        const std::filesystem::path& dest,
                        const GifSpec& spec,
                        std::chrono::milliseconds timeout);

}  // namespace xdl
