# xdl

Download videos and animated GIFs from Twitter/X posts — a C++23 library and a
command-line tool built on it.

`xdl` resolves a post's media through Twitter's public embed endpoint, picks the
highest-bitrate MP4 available, and optionally re-encodes it to a real `.gif`
using a two-pass palette.

```console
$ xdl https://x.com/NASA/status/1491475671058681863?s=20
[ok]   ~/Downloads/1491475671058681863.mp4  (17509 KiB)
```

```cpp
xdl::CurlHttpClient http;
xdl::SyndicationSource source{http};

auto id    = xdl::extract_tweet_id("https://x.com/NASA/status/1491475671058681863?s=20");
auto media = source.fetch(*id, std::chrono::seconds{30});

for (const auto& item : *media) {
  std::println("{} {}", xdl::to_string(item.kind), item.url);
}
```

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Install](#install)
- [Command-line usage](#command-line-usage)
- [Library usage](#library-usage)
  - [Adding it to a project](#adding-it-to-a-project)
  - [Core types](#core-types)
  - [Resolving media](#resolving-media)
  - [Downloading and converting](#downloading-and-converting)
  - [Substituting your own I/O](#substituting-your-own-io)
  - [API reference](#api-reference)
- [Development](#development)
- [Known limitations](#known-limitations)

## Features

- **Videos and animated GIFs.** Twitter stores both as MP4; `--only` filters by
  kind when you want just one.
- **GIF conversion.** Two-pass palette encoding via ffmpeg, with `--start` and
  `--duration` to take a slice rather than the whole clip.
- **Batch downloads.** Multiple URLs, `--from-file`, or stdin, fetched across a
  configurable worker pool.
- **Tolerant of share links.** Tracking parameters (`?s=20`, `?t=…`,
  `ref_src`, …) and fragments are stripped, so different spellings of the same
  post collapse to a single download.
- **Two backends.** The public syndication endpoint by default, with a `yt-dlp`
  fallback for posts it cannot serve.
- **Embeddable.** Every layer is a library API; the CLI is a thin wrapper. All
  I/O sits behind interfaces you can replace.

## Requirements

| | |
| --- | --- |
| Build | C++23 compiler, CMake 3.28+ |
| Link | libcurl, [simdjson](https://simdjson.org) |
| Runtime | ffmpeg (only for `--gif`), yt-dlp (only for the fallback backend) |

Developed against Apple Clang 21 on macOS, where libcurl ships with the SDK.

## Install

```sh
brew install simdjson cmake ffmpeg yt-dlp     # macOS
# apt install libsimdjson-dev cmake libcurl4-openssl-dev ffmpeg  # Debian/Ubuntu

git clone https://github.com/jcongc/xdl.git
cd xdl
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix ~/.local   # puts xdl on your PATH
```

## Command-line usage

```
xdl [URL|ID|-]... [options]
```

Accepts full post URLs, bare status ids, or `-` to read a newline-separated list
from stdin. Blank lines and `#` comments are ignored.

| Option | Default | Description |
| --- | --- | --- |
| `-o`, `--outdir DIR` | `~/Downloads` | Where files are written |
| `--only {all,gif,video}` | `all` | Restrict to one media kind |
| `--gif` | off | Re-encode to a real `.gif` (requires ffmpeg) |
| `--no-keep-mp4` | off | Delete the intermediate MP4 after `--gif` |
| `--fps N` | `15` | GIF frame rate |
| `--width N` | `480` | GIF width in pixels; `-1` keeps the source size |
| `--start SEC` | `0` | Skip this many seconds before the GIF begins |
| `--duration SEC` | whole clip | Convert only this many seconds |
| `--from-file PATH` | — | Read URLs from a file, one per line |
| `--backend {auto,syndication,yt-dlp}` | `auto` | Media resolution backend |
| `-j`, `--jobs N` | `4` | Parallel downloads |
| `-f`, `--overwrite` | off | Re-download files that already exist |
| `--timeout SEC` | `30` | Per-request timeout |

Files are named `<status_id>.mp4` / `.gif`, or `<status_id>_<n>` when a post
carries several media items.

`--gif` on a full-length video produces an enormous file — a three-minute clip
is not a reasonable GIF. `xdl` warns above 30 seconds; use `--start` and
`--duration` to take the part you want:

```sh
xdl --gif --start 12 --duration 4 --fps 12 --width 400 https://x.com/user/status/123
```

Exit status is `0` when every post was processed, `1` when at least one failed,
and `2` for invalid arguments or no input.

## Library usage

All functionality lives in the `xdl_core` target. `app/main.cpp` is only
argument parsing and wiring, so anything the CLI does is reachable from code.

### Adding it to a project

The library is not yet published as an installable CMake package. Consume it as
a subdirectory, turning off its test suite so your build does not pull in
GoogleTest:

```cmake
set(XDL_BUILD_TESTS OFF)
add_subdirectory(third_party/xdl)

target_link_libraries(your_app PRIVATE xdl_core)
```

Or with `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(xdl
  GIT_REPOSITORY https://github.com/jcongc/xdl.git
  GIT_TAG        main)
set(XDL_BUILD_TESTS OFF)
FetchContent_MakeAvailable(xdl)

target_link_libraries(your_app PRIVATE xdl_core)
```

`xdl_core` propagates its include directory and link requirements, so no further
configuration is needed. Compiling by hand works too:

```sh
c++ -std=c++23 -Ixdl/include app.cpp xdl/build/release/libxdl_core.a \
    $(pkg-config --cflags --libs simdjson) -lcurl -o app
```

### Core types

Errors are values rather than exceptions — every fallible call says so in its
signature, and nothing unwinds across a thread boundary.

```cpp
#include <xdl/error.hpp>
#include <xdl/media.hpp>

struct xdl::Error {
  enum class Kind { BadInput, Network, Unavailable, NoMedia, Subprocess, Io };
  Kind kind;
  std::string message;
};

template <class T> using xdl::Result = std::expected<T, xdl::Error>;

enum class xdl::MediaKind { Gif, Video };

struct xdl::Media {
  std::string url;
  MediaKind   kind;
  int         duration_ms;
};
```

### Resolving media

`extract_tweet_id` accepts a post URL or a bare id, normalising away tracking
parameters first. A `MediaSource` turns that id into `Media` records.

```cpp
#include <xdl/http.hpp>
#include <xdl/syndication.hpp>
#include <xdl/url.hpp>

const xdl::CurlGlobal curl_guard;   // once per process, before any threads
xdl::CurlHttpClient http;           // safe to share; uses a handle per thread
xdl::SyndicationSource source{http};

auto id = xdl::extract_tweet_id("https://x.com/NASA/status/1491475671058681863?s=20");
if (!id) {
  std::println(stderr, "bad url: {}", id.error().message);
  return 1;
}

auto media = source.fetch(*id, std::chrono::seconds{30});
if (!media) {
  std::println(stderr, "fetch failed: {}", media.error().message);
  return 1;
}

for (const auto& item : *media) {
  std::println("{} {} ({} ms)", xdl::to_string(item.kind), item.url, item.duration_ms);
}
```

To try several backends in order, hand them to `resolve_media`, which reports
every failure together if all of them fail:

```cpp
#include <xdl/backend.hpp>
#include <xdl/ytdlp.hpp>

xdl::PosixProcessRunner runner;
xdl::YtDlpSource fallback{runner};

xdl::MediaSource* sources[] = {&source, &fallback};
auto media = xdl::resolve_media(sources, *id, xdl::OnlyFilter::All,
                                std::chrono::seconds{30});
```

### Downloading and converting

`Downloader` is the whole pipeline: resolve, download, optionally convert. It
takes an `Options` whose defaults match the CLI's.

```cpp
#include <xdl/downloader.hpp>

xdl::Options options;
options.outdir   = "/tmp/clips";
options.make_gif = true;
options.duration = 4.0;          // seconds; omit for the whole clip
options.jobs     = 8;

xdl::CurlHttpClient http;
xdl::PosixProcessRunner runner;
xdl::Downloader downloader{http, runner, options};

// process_one for a single post...
const xdl::SourceResult one = downloader.process_one("https://x.com/user/status/123");

// ...or run for a batch, fanned out across options.jobs workers.
// Results come back in input order regardless of completion order.
const std::vector<xdl::SourceResult> many = downloader.run(sources);

for (const auto& result : many) {
  if (result.error) {
    std::println(stderr, "{}: {}", result.source, result.error->message);
  }
  for (const auto& note : result.notes) std::println(stderr, "warning: {}", note);
  for (const auto& file : result.files) std::println("{}", file.string());
}
```

Use `collect_sources` to apply the CLI's input handling — normalise, drop blanks
and `#` comments, and deduplicate while preserving order:

```cpp
const auto sources = xdl::collect_sources(raw_lines);
```

### Substituting your own I/O

Everything that reaches the outside world goes through two interfaces. Replace
either to add caching, retries, request logging, or a mock for your own tests.

```cpp
class HttpClient {
 public:
  virtual Result<HttpResponse> get(std::string_view url, const Headers&,
                                   std::chrono::milliseconds timeout) = 0;
  virtual Result<void> download(std::string_view url,
                                const std::filesystem::path& dest,
                                std::chrono::milliseconds timeout) = 0;
};

class ProcessRunner {
 public:
  virtual Result<ProcessResult> run(std::span<const std::string> argv,
                                    std::chrono::milliseconds timeout) = 0;
};
```

A stub client is enough to drive the pipeline with no network at all — the same
approach the test suite uses:

```cpp
class StubHttp final : public xdl::HttpClient {
 public:
  xdl::Result<xdl::HttpResponse> get(std::string_view, const xdl::Headers&,
                                     std::chrono::milliseconds) override {
    return xdl::HttpResponse{200, my_canned_payload};
  }
  xdl::Result<void> download(std::string_view, const std::filesystem::path&,
                             std::chrono::milliseconds) override {
    return {};
  }
};

StubHttp http;
xdl::SyndicationSource source{http};
auto media = source.fetch("123", std::chrono::seconds{5});
```

### API reference

| Header | Provides |
| --- | --- |
| `xdl/error.hpp` | `Error`, `Error::Kind`, `Result<T>`, `fail()` |
| `xdl/media.hpp` | `Media`, `MediaKind`, `to_string` |
| `xdl/options.hpp` | `Options`, `OnlyFilter`, `BackendChoice` |
| `xdl/url.hpp` | `normalize_url`, `extract_tweet_id` |
| `xdl/token.hpp` | `syndication_token` |
| `xdl/http.hpp` | `HttpClient`, `CurlHttpClient`, `CurlGlobal`, `Headers`, `HttpResponse` |
| `xdl/process.hpp` | `ProcessRunner`, `PosixProcessRunner`, `ProcessResult`, `program_exists` |
| `xdl/backend.hpp` | `MediaSource`, `resolve_media`, `filter_media` |
| `xdl/syndication.hpp` | `SyndicationSource`, `parse_syndication_payload`, `build_syndication_url` |
| `xdl/ytdlp.hpp` | `YtDlpSource`, `parse_ytdlp_payload` |
| `xdl/ffmpeg.hpp` | `GifSpec`, `encode_gif`, `build_palettegen_argv`, `build_paletteuse_argv` |
| `xdl/thread_pool.hpp` | `ThreadPool` |
| `xdl/downloader.hpp` | `Downloader`, `SourceResult`, `collect_sources` |
| `xdl/cli.hpp` | `parse_args`, `usage`, `read_sources_file`, `read_sources_stdin` |

**Threading.** Construct one `CurlGlobal` per process before starting threads;
`curl_global_init` is not thread-safe. `CurlHttpClient` is then safe to share
across threads — it borrows a `thread_local` easy handle per call, because
libcurl forbids concurrent use of a single handle.

## Development

```sh
cmake --preset debug      # AddressSanitizer + UndefinedBehaviorSanitizer, -Werror
cmake --build --preset debug
ctest --preset debug
```

The test suite runs entirely offline, driven by captured payloads in
`tests/fixtures/` — no network access and no subprocesses.

```
include/xdl/   public headers
src/           implementations
app/main.cpp   argument parsing and wiring
tests/         test suite, fakes, and fixtures
```

Two invariants are worth preserving:

- **No simdjson type appears in a header.** Its on-demand API returns views into
  the parser's buffer, so parsing is confined to `syndication.cpp` and
  `ytdlp.cpp`, and every value is copied into an owned `Media` before being
  returned.
- **Subprocesses never go through a shell.** `posix_spawn` receives an explicit
  argument vector, so URLs and filenames cannot be interpreted as commands.

## Known limitations

- The `yt-dlp` backend infers media kind from the CDN path, because yt-dlp
  reports `acodec` as null for Twitter's progressive formats regardless of
  whether audio exists. Posts served from unfamiliar paths are treated as
  videos.
- Only Twitter/X is supported. Protected, deleted, and age-gated posts cannot be
  fetched.
- `xdl_core` has no install or `find_package` support yet; consume it via
  `add_subdirectory` or `FetchContent`.
- A Homebrew-provided simdjson leaves the binary linked against `/opt/homebrew`,
  so it is not portable to machines without it.
