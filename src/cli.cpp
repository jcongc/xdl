#include "xdl/cli.hpp"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace xdl {
namespace {

std::filesystem::path default_outdir() {
  if (const char* home = std::getenv("HOME"); home != nullptr) {
    return std::filesystem::path{home} / "Downloads";
  }
  return std::filesystem::current_path();
}

Result<int> parse_int(std::string_view text, std::string_view flag) {
  int value = 0;
  const auto* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(text.data(), end, value);
  if (ec != std::errc{} || ptr != end) {
    return fail(Error::Kind::BadInput,
                std::string{flag} + " expects an integer, got '" + std::string{text} + "'");
  }
  return value;
}

Result<double> parse_double(std::string_view text, std::string_view flag) {
  // from_chars for double is available but std::stod gives clearer failure
  // handling here; the string is short and always ASCII.
  try {
    size_t consumed = 0;
    const std::string owned{text};
    const double value = std::stod(owned, &consumed);
    if (consumed != owned.size()) {
      throw std::invalid_argument{"trailing characters"};
    }
    return value;
  } catch (const std::exception&) {
    return fail(Error::Kind::BadInput,
                std::string{flag} + " expects a number, got '" + std::string{text} + "'");
  }
}

// Pulls the value that follows a flag, erroring rather than reading past the
// end when the user forgets it.
Result<std::string_view> take_value(std::span<const std::string> args, size_t& i,
                                    std::string_view flag) {
  if (i + 1 >= args.size()) {
    return fail(Error::Kind::BadInput, std::string{flag} + " expects a value");
  }
  return std::string_view{args[++i]};
}

}  // namespace

std::string usage() {
  return R"(usage: xdl [-h] [--from-file PATH] [-o OUTDIR] [--only {all,gif,video}]
           [--gif] [--no-keep-mp4] [--fps FPS] [--width WIDTH]
           [--start SEC] [--duration SEC]
           [--backend {auto,syndication,yt-dlp}] [-j JOBS] [-f]
           [--timeout SEC]
           [URL ...]

Download videos and animated GIFs from Twitter/X posts.

positional arguments:
  URL                   tweet URLs, bare status ids, or '-' for stdin

options:
  -h, --help            show this help message and exit
  --from-file PATH      read URLs from a file, one per line
  -o, --outdir DIR      output dir (~/Downloads)
  --only {all,gif,video}
                        restrict to one media kind (all)
  --gif                 re-encode to a real .gif (needs ffmpeg)
  --no-keep-mp4         delete the MP4 after --gif
  --fps FPS             gif frame rate (15)
  --width WIDTH         gif width in px, -1 keeps source (480)
  --start SEC           skip SEC seconds before the gif starts (0)
  --duration SEC        convert only SEC seconds to gif (whole clip)
  --backend {auto,syndication,yt-dlp}
  -j, --jobs JOBS       parallel downloads (4)
  -f, --overwrite       re-download existing files
  --timeout SEC         per-request timeout (30s)

Twitter GIFs are MP4 under the hood; use --gif to re-encode.
)";
}

Result<ParsedArgs> parse_args(std::span<const std::string> args) {
  ParsedArgs parsed;
  parsed.options.outdir = default_outdir();
  std::string from_file;

  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];

    if (arg == "-h" || arg == "--help") {
      parsed.help = true;
      return parsed;
    }

    if (arg == "--gif") {
      parsed.options.make_gif = true;
    } else if (arg == "--no-keep-mp4") {
      parsed.options.keep_mp4 = false;
    } else if (arg == "-f" || arg == "--overwrite") {
      parsed.options.overwrite = true;
    } else if (arg == "-o" || arg == "--outdir") {
      auto value = take_value(args, i, arg);
      if (!value) return std::unexpected(value.error());
      parsed.options.outdir = std::filesystem::path{*value};
    } else if (arg == "--from-file") {
      auto value = take_value(args, i, arg);
      if (!value) return std::unexpected(value.error());
      from_file = std::string{*value};
    } else if (arg == "--only") {
      auto value = take_value(args, i, arg);
      if (!value) return std::unexpected(value.error());
      if (*value == "all") {
        parsed.options.only = OnlyFilter::All;
      } else if (*value == "gif") {
        parsed.options.only = OnlyFilter::Gif;
      } else if (*value == "video") {
        parsed.options.only = OnlyFilter::Video;
      } else {
        return fail(Error::Kind::BadInput,
                    "--only expects one of {all,gif,video}, got '" +
                        std::string{*value} + "'");
      }
    } else if (arg == "--backend") {
      auto value = take_value(args, i, arg);
      if (!value) return std::unexpected(value.error());
      if (*value == "auto") {
        parsed.options.backend = BackendChoice::Auto;
      } else if (*value == "syndication") {
        parsed.options.backend = BackendChoice::Syndication;
      } else if (*value == "yt-dlp") {
        parsed.options.backend = BackendChoice::YtDlp;
      } else {
        return fail(Error::Kind::BadInput,
                    "--backend expects one of {auto,syndication,yt-dlp}, got '" +
                        std::string{*value} + "'");
      }
    } else if (arg == "--fps") {
      auto value = take_value(args, i, arg);
      if (!value) return std::unexpected(value.error());
      auto n = parse_int(*value, arg);
      if (!n) return std::unexpected(n.error());
      parsed.options.fps = *n;
    } else if (arg == "--width") {
      auto value = take_value(args, i, arg);
      if (!value) return std::unexpected(value.error());
      auto n = parse_int(*value, arg);
      if (!n) return std::unexpected(n.error());
      parsed.options.width = *n;
    } else if (arg == "-j" || arg == "--jobs") {
      auto value = take_value(args, i, arg);
      if (!value) return std::unexpected(value.error());
      auto n = parse_int(*value, arg);
      if (!n) return std::unexpected(n.error());
      if (*n < 1) {
        return fail(Error::Kind::BadInput, "--jobs must be at least 1");
      }
      parsed.options.jobs = static_cast<unsigned>(*n);
    } else if (arg == "--start") {
      auto value = take_value(args, i, arg);
      if (!value) return std::unexpected(value.error());
      auto d = parse_double(*value, arg);
      if (!d) return std::unexpected(d.error());
      parsed.options.start = *d;
    } else if (arg == "--duration") {
      auto value = take_value(args, i, arg);
      if (!value) return std::unexpected(value.error());
      auto d = parse_double(*value, arg);
      if (!d) return std::unexpected(d.error());
      parsed.options.duration = *d;
    } else if (arg == "--timeout") {
      auto value = take_value(args, i, arg);
      if (!value) return std::unexpected(value.error());
      auto d = parse_double(*value, arg);
      if (!d) return std::unexpected(d.error());
      parsed.options.timeout =
          std::chrono::milliseconds{static_cast<long long>(*d * 1000.0)};
    } else if (arg.size() > 1 && arg.front() == '-' && arg != "-") {
      return fail(Error::Kind::BadInput, "unrecognized argument: " + arg);
    } else {
      parsed.sources.push_back(arg);
    }
  }

  if (!from_file.empty()) {
    auto lines = read_sources_file(from_file);
    if (!lines) {
      return std::unexpected(lines.error());
    }
    parsed.sources.insert(parsed.sources.end(), lines->begin(), lines->end());
  }

  return parsed;
}

Result<std::vector<std::string>> read_sources_file(const std::string& path) {
  std::ifstream in{path};
  if (!in) {
    return fail(Error::Kind::Io, "cannot read " + path);
  }
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) {
    lines.push_back(line);
  }
  return lines;
}

std::vector<std::string> read_sources_stdin() {
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(std::cin, line)) {
    lines.push_back(line);
  }
  return lines;
}

}  // namespace xdl
