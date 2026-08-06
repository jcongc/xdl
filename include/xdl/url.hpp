#pragma once

#include <string>
#include <string_view>

#include "xdl/error.hpp"

namespace xdl {

// Strips share/analytics query parameters and any #fragment from a tweet URL.
//
//   https://x.com/u/status/123?s=20&t=abc  ->  https://x.com/u/status/123
//
// Parameters outside the strip list (e.g. lang=en) are preserved. Input that
// is not a URL at all — a bare status id, a comment line — is returned as-is.
std::string normalize_url(std::string_view raw);

// Pulls the numeric status id out of a tweet URL, or accepts a bare id.
// Recognises twitter.com, x.com, and the usual embed-fixer mirrors.
Result<std::string> extract_tweet_id(std::string_view raw);

}  // namespace xdl
