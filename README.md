# xdl-cpp

Download videos and animated GIFs from Twitter/X posts.

A C++23 port of a Python tool of the same name, written as a learning project.
Twitter stores "GIFs" as silent looping MP4s (media type `animated_gif`) and
regular videos as `video`; this grabs the highest-bitrate MP4 variant of either
and can re-encode it to a real `.gif` through ffmpeg using a two-pass palette.

```console
$ xdl https://x.com/NASA/status/1491475671058681863?s=20
[ok]   ~/Downloads/1491475671058681863.mp4  (17509 KiB)

$ xdl --gif --start 3 --duration 5 --width 320 https://x.com/user/status/123
[ok]   ~/Downloads/123.gif  (573 KiB)
[ok]   ~/Downloads/123.mp4  (199 KiB)
```

## Build

Requires a C++23 compiler, CMake 3.28+, libcurl, and simdjson. On macOS libcurl
comes with the SDK; simdjson comes from Homebrew.

```sh
brew install simdjson cmake        # ffmpeg for --gif, yt-dlp for the fallback
cmake --preset release
cmake --build --preset release
./build/release/xdl --help
```

The `debug` preset adds AddressSanitizer, UndefinedBehaviorSanitizer, and
`-Werror`:

```sh
cmake --preset debug && cmake --build --preset debug
ctest --preset debug
```

Tests run entirely offline against captured fixtures — no network, no
subprocesses.

## Usage

```
xdl [URL|ID|-]... [--from-file PATH] [-o DIR]
    [--only {all,gif,video}] [--gif] [--no-keep-mp4]
    [--fps N] [--width N] [--start SEC] [--duration SEC]
    [--backend {auto,syndication,yt-dlp}] [-j N] [-f] [--timeout SEC]
```

Accepts full URLs, bare status ids, or `-` to read a list from stdin. Share
parameters (`?s=20`, `?t=…`, `ref_src`, …) are stripped, so the three spellings
of a link you get from X's share menu all collapse to one download.

`--width -1` keeps the source resolution. `--start` and `--duration` matter for
videos: a 205-second clip makes an unreasonable GIF, and the tool warns before
producing one.

### Backends

| Backend | Mechanism |
|---|---|
| `syndication` | The public `cdn.syndication.twimg.com` embed endpoint. No auth, no API key. |
| `yt-dlp` | Shells out to `yt-dlp --dump-single-json`. More robust when Twitter changes things. |

`auto` tries syndication first and falls back to yt-dlp.

## Layout

```
include/xdl/   public headers — the library's contract
src/           implementations
app/main.cpp   argv parsing and wiring only
tests/         GoogleTest suite, fakes, and captured fixtures
```

All logic lives in a static `xdl_core` library. Everything touching the outside
world goes through two abstract interfaces, `HttpClient` and `ProcessRunner`,
which tests replace with fakes — that is what makes the whole pipeline testable
without a network connection.

Two conventions worth preserving if you change things:

- **No simdjson type appears in any header.** Its on-demand API returns views
  into the parser's buffer, so parsing stays inside `syndication.cpp` and
  `ytdlp.cpp` and every value is copied into an owned `Media` before it is
  returned. Dangling views become impossible rather than merely unlikely.
- **Subprocesses never go through a shell.** `posix_spawn` takes an explicit
  argv vector, so URLs and filenames are inert data.

## Notes

Two ffmpeg argument-ordering rules are load-bearing, and both are pinned by
tests in `tests/ffmpeg_test.cpp`:

1. `-t` must follow the *last* `-i`. The paletteuse pass has two inputs; a `-t`
   between them is read as an input option for the palette and the entire clip
   gets encoded — a 3-second request once produced a 46 MB GIF.
2. When width is not positive the `scale` filter is omitted entirely, because
   `scale=-1:-1` is not valid.

yt-dlp exposes no `animated_gif` flag, so the fallback backend treats a silent
stream as a GIF. A genuinely silent video is misfiled as a GIF; this is a known
limitation inherited from the original tool.
