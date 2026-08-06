#include "xdl/token.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>

namespace xdl {
namespace {

constexpr char kBase36[] = "0123456789abcdefghijklmnopqrstuvwxyz";

// JS prints roughly as many fractional digits as the double carries; 24 is
// comfortably past the point where they stop mattering, and matches the
// reference Python implementation exactly.
constexpr int kFractionDigits = 24;

}  // namespace

std::string syndication_token(std::string_view tweet_id) {
  std::uint64_t id = 0;
  const auto* first = tweet_id.data();
  const auto* last = tweet_id.data() + tweet_id.size();
  if (std::from_chars(first, last, id).ec != std::errc{}) {
    return {};
  }

  const double n = (static_cast<double>(id) / 1e15) * std::numbers::pi_v<double>;

  double whole_part = 0.0;
  double frac = std::modf(n, &whole_part);
  auto whole = static_cast<std::uint64_t>(whole_part);

  std::string head;
  if (whole == 0) {
    head = "0";
  }
  while (whole > 0) {
    head.insert(head.begin(), kBase36[whole % 36]);
    whole /= 36;
  }

  std::string tail;
  tail.reserve(kFractionDigits);
  for (int i = 0; i < kFractionDigits; ++i) {
    frac *= 36.0;
    const auto digit = static_cast<int>(frac);
    tail.push_back(kBase36[digit]);
    frac -= static_cast<double>(digit);
  }

  // The JS does .replace(/(0+|\.)/g, ''), which deletes every '0' and the
  // decimal point — not just leading or trailing runs.
  std::string combined = head + "." + tail;
  std::string out;
  out.reserve(combined.size());
  for (const char c : combined) {
    if (c != '0' && c != '.') {
      out.push_back(c);
    }
  }
  return out;
}

}  // namespace xdl
