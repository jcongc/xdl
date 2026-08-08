# xdl

Download videos and animated GIFs from Twitter/X posts.

`xdl` resolves a post's media through Twitter's public embed endpoint, picks the
highest-bitrate MP4 available, and optionally re-encodes it to a real `.gif`
using a two-pass palette. It takes URLs, bare status ids, or a list on stdin,
and fetches them in parallel.

```console
$ xdl https://x.com/NASA/status/1491475671058681863?s=20
[ok]   ~/Downloads/1491475671058681863.mp4  (17509 KiB)

$ xdl --gif --start 3 --duration 5 --width 320 https://x.com/user/status/123
[ok]   ~/Downloads/123.gif  (573 KiB)
[ok]   ~/Downloads/123.mp4  (199 KiB)
```

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
- **Resumable-safe writes.** Downloads land in a `.part` file and are renamed on
  success, so an interrupted run never leaves a truncated file behind.

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

git clone https://github.com/jcongc/xdl-cpp.git
cd xdl-cpp
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix ~/.local   # puts xdl on your PATH
```

## Usage

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

### Converting videos

`--gif` on a full-length video produces an enormous file — a three-minute clip
is not a reasonable GIF. `xdl` warns above 30 seconds; use `--start` and
`--duration` to take the part you want, and lower `--fps` or `--width` to
control size.

```sh
xdl --gif --start 12 --duration 4 --fps 12 --width 400 https://x.com/user/status/123
```

### Backends

| Backend | Mechanism |
| --- | --- |
| `syndication` | Twitter's public `cdn.syndication.twimg.com` embed endpoint. No authentication or API key. |
| `yt-dlp` | Shells out to `yt-dlp --dump-single-json`. Slower, but more robust when Twitter changes its payloads. |

`auto` tries syndication first and falls back to yt-dlp. Failures from every
attempted backend are reported together.

## Exit status

| Code | Meaning |
| --- | --- |
| `0` | All posts processed |
| `1` | At least one post failed |
| `2` | Invalid arguments, or no input given |

## Development

```sh
cmake --preset debug      # AddressSanitizer + UndefinedBehaviorSanitizer, -Werror
cmake --build --preset debug
ctest --preset debug
```

The test suite runs entirely offline, driven by captured payloads in
`tests/fixtures/` — no network access and no subprocesses.

### Architecture

All logic lives in a static `xdl_core` library; `app/main.cpp` only parses
arguments and wires up implementations. Everything that touches the outside
world goes through two abstract interfaces, `HttpClient` and `ProcessRunner`,
which the tests replace with fakes.

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
- A Homebrew-provided simdjson leaves the binary linked against `/opt/homebrew`,
  so it is not portable to machines without it.
