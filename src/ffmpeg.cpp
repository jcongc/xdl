#include "xdl/ffmpeg.hpp"

#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <vector>

#include "xdl/process.hpp"

namespace xdl {
namespace {

// ffmpeg wants plain decimal seconds; std::format gives us that without the
// locale surprises of ostringstream.
std::string seconds(double value) {
  std::string s = std::format("{:.6f}", value);
  // Trim trailing zeros, then a trailing '.', so 3.0 prints as "3".
  const auto last = s.find_last_not_of('0');
  s.erase(last + 1);
  if (!s.empty() && s.back() == '.') {
    s.pop_back();
  }
  return s;
}

void append_trim_in(std::vector<std::string>& argv, const GifSpec& spec) {
  if (spec.start > 0.0) {
    argv.emplace_back("-ss");
    argv.push_back(seconds(spec.start));
  }
}

void append_trim_out(std::vector<std::string>& argv, const GifSpec& spec) {
  if (spec.duration && *spec.duration > 0.0) {
    argv.emplace_back("-t");
    argv.push_back(seconds(*spec.duration));
  }
}

}  // namespace

std::string build_filter_chain(const GifSpec& spec) {
  std::string chain = std::format("fps={}", spec.fps);
  // width <= 0 means "keep the source size"; emitting scale=-1:-1 would be
  // rejected by ffmpeg, so the filter is dropped entirely.
  if (spec.width > 0) {
    chain += std::format(",scale={}:-1:flags=lanczos", spec.width);
  }
  return chain;
}

std::vector<std::string> build_palettegen_argv(const std::filesystem::path& src,
                                               const std::filesystem::path& palette,
                                               const GifSpec& spec) {
  std::vector<std::string> argv{"ffmpeg", "-v", "error", "-y"};
  append_trim_in(argv, spec);
  argv.emplace_back("-i");
  argv.push_back(src.string());
  append_trim_out(argv, spec);
  argv.emplace_back("-vf");
  argv.push_back(build_filter_chain(spec) + ",palettegen=stats_mode=diff");
  argv.push_back(palette.string());
  return argv;
}

std::vector<std::string> build_paletteuse_argv(const std::filesystem::path& src,
                                               const std::filesystem::path& palette,
                                               const std::filesystem::path& dest,
                                               const GifSpec& spec) {
  std::vector<std::string> argv{"ffmpeg", "-v", "error", "-y"};
  append_trim_in(argv, spec);
  argv.emplace_back("-i");
  argv.push_back(src.string());
  argv.emplace_back("-i");
  argv.push_back(palette.string());
  // -t MUST land after the last -i. Between the two inputs it would be parsed
  // as an input option for the palette and the entire clip would be encoded.
  append_trim_out(argv, spec);
  argv.emplace_back("-lavfi");
  argv.push_back(build_filter_chain(spec) +
                 "[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=5"
                 ":diff_mode=rectangle");
  argv.emplace_back("-loop");
  argv.emplace_back("0");
  argv.push_back(dest.string());
  return argv;
}

Result<void> encode_gif(ProcessRunner& runner,
                        const std::filesystem::path& src,
                        const std::filesystem::path& dest,
                        const GifSpec& spec,
                        std::chrono::milliseconds timeout) {
  if (!program_exists("ffmpeg")) {
    return fail(Error::Kind::Subprocess,
                "ffmpeg not found on PATH; drop --gif or install ffmpeg");
  }

  std::error_code ec;
  const auto temp_dir = std::filesystem::temp_directory_path(ec) /
                        ("xdl-" + dest.stem().string() + "-palette");
  if (ec) {
    return fail(Error::Kind::Io, "cannot locate temp directory: " + ec.message());
  }
  std::filesystem::create_directories(temp_dir, ec);
  if (ec) {
    return fail(Error::Kind::Io, "cannot create temp directory: " + ec.message());
  }

  // Clean the palette directory on every exit path, including the error ones.
  struct Cleanup {
    std::filesystem::path dir;
    ~Cleanup() {
      std::error_code ignored;
      std::filesystem::remove_all(dir, ignored);
    }
  } cleanup{temp_dir};

  const auto palette = temp_dir / "palette.png";

  for (const auto& argv : {build_palettegen_argv(src, palette, spec),
                           build_paletteuse_argv(src, palette, dest, spec)}) {
    auto res = runner.run(argv, timeout);
    if (!res) {
      return std::unexpected(res.error());
    }
    if (res->exit_code != 0) {
      std::string detail = res->err.empty() ? res->out : res->err;
      return fail(Error::Kind::Subprocess, "ffmpeg: " + detail);
    }
  }

  return {};
}

}  // namespace xdl
