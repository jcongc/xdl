#pragma once

#include <string>
#include <string_view>

namespace xdl {

// Reproduces the token the Twitter embed widget computes in JavaScript:
//
//     ((id / 1e15) * Math.PI).toString(36).replace(/(0+|\.)/g, '')
//
// The syndication endpoint rejects requests without it. Matching JS here means
// matching double-precision arithmetic and JS's base36 fractional expansion,
// so this function is pinned by unit tests against known-good outputs rather
// than trusted to be "obviously right".
std::string syndication_token(std::string_view tweet_id);

}  // namespace xdl
