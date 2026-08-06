#include <algorithm>
#include <filesystem>
#include <print>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "xdl/cli.hpp"
#include "xdl/downloader.hpp"
#include "xdl/http.hpp"
#include "xdl/process.hpp"

namespace {

double kib(const std::filesystem::path& path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  return ec ? 0.0 : static_cast<double>(size) / 1024.0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);

  auto parsed = xdl::parse_args(args);
  if (!parsed) {
    std::println(stderr, "xdl: {}", parsed.error().message);
    std::print(stderr, "\n{}", xdl::usage());
    return 2;
  }
  if (parsed->help) {
    std::print("{}", xdl::usage());
    return 0;
  }

  // '-' means "read the list from stdin", matching the Python tool.
  std::vector<std::string> raw;
  for (const auto& source : parsed->sources) {
    if (source == "-") {
      const auto piped = xdl::read_sources_stdin();
      raw.insert(raw.end(), piped.begin(), piped.end());
    } else {
      raw.push_back(source);
    }
  }

  const auto sources = xdl::collect_sources(raw);
  if (sources.empty()) {
    std::print(stderr, "{}", xdl::usage());
    return 2;
  }

  // curl_global_init is not thread-safe; this guard runs it once, before any
  // worker exists, and cleans up after the pool is gone.
  const xdl::CurlGlobal curl_guard;
  xdl::CurlHttpClient http;
  xdl::PosixProcessRunner runner;

  xdl::Downloader downloader{http, runner, parsed->options};
  const auto results = downloader.run(sources);

  size_t failures = 0;
  for (const auto& result : results) {
    if (result.error) {
      ++failures;
      std::println(stderr, "[fail] {}\n       {}", result.source,
                   result.error->message);
    }
    for (const auto& note : result.notes) {
      std::println(stderr, "[warn] {}", note);
    }
    for (const auto& file : result.files) {
      std::println("[ok]   {}  ({:.0f} KiB)", file.string(), kib(file));
    }
  }

  std::println(stderr, "\n{}/{} tweets processed{}", results.size() - failures,
               results.size(),
               failures > 0 ? std::format(", {} failed", failures) : "");

  return failures > 0 ? 1 : 0;
}
