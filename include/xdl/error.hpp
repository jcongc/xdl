#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace xdl {

// Errors are values, not exceptions: every fallible operation says so in its
// signature, and nothing has to unwind across a thread boundary.
struct Error {
  enum class Kind {
    BadInput,     // malformed URL, bad flag, no tweet id
    Network,      // transport failure or non-2xx response
    Unavailable,  // tweet deleted, protected, or age-gated
    NoMedia,      // tweet exists but carries nothing we can download
    Subprocess,   // ffmpeg / yt-dlp missing or exited non-zero
    Io,           // filesystem failure
  };

  Kind kind{Kind::BadInput};
  std::string message;
};

template <class T>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> fail(Error::Kind kind, std::string message) {
  return std::unexpected(Error{kind, std::move(message)});
}

constexpr std::string_view to_string(Error::Kind kind) {
  switch (kind) {
    case Error::Kind::BadInput:    return "bad-input";
    case Error::Kind::Network:     return "network";
    case Error::Kind::Unavailable: return "unavailable";
    case Error::Kind::NoMedia:     return "no-media";
    case Error::Kind::Subprocess:  return "subprocess";
    case Error::Kind::Io:          return "io";
  }
  return "unknown";
}

}  // namespace xdl
