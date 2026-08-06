#include "xdl/url.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace xdl {
namespace {

// Share/analytics junk X appends to copied links.
constexpr std::array kTrackingParams{
    std::string_view{"s"},       std::string_view{"t"},
    std::string_view{"cxt"},     std::string_view{"src"},
    std::string_view{"ref_src"}, std::string_view{"ref_url"},
    std::string_view{"context"}, std::string_view{"twclid"},
    std::string_view{"si"},
};

std::string lowered(std::string_view in) {
  std::string out(in);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

bool is_tracking_param(std::string_view key) {
  const std::string k = lowered(key);
  return std::find(kTrackingParams.begin(), kTrackingParams.end(), k) !=
         kTrackingParams.end();
}

std::string_view trimmed(std::string_view s) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  auto begin = std::find_if(s.begin(), s.end(), not_space);
  auto end = std::find_if(s.rbegin(), s.rend(), not_space).base();
  return begin < end ? std::string_view{&*begin, static_cast<size_t>(end - begin)}
                     : std::string_view{};
}

// twitter.com, x.com, and the usual embed-fixer mirrors.
const std::regex& status_regex() {
  static const std::regex re{
      R"((?:twitter|x|fxtwitter|fixupx|vxtwitter|twittpr|nitter\.[\w.]+)\.com)"
      R"(/[^/]+/status(?:es)?/(\d+))",
      std::regex::icase | std::regex::optimize};
  return re;
}

const std::regex& bare_id_regex() {
  static const std::regex re{R"(^\d{5,25}$)", std::regex::optimize};
  return re;
}

}  // namespace

std::string normalize_url(std::string_view raw) {
  const std::string_view input = trimmed(raw);

  // Not a URL (a bare id, a comment) — hand it back untouched.
  if (input.find("://") == std::string_view::npos) {
    return std::string{input};
  }

  std::string_view rest = input;

  // Drop the fragment first; it can contain '?' which would confuse the split.
  if (const auto hash = rest.find('#'); hash != std::string_view::npos) {
    rest = rest.substr(0, hash);
  }

  const auto qmark = rest.find('?');
  if (qmark == std::string_view::npos) {
    return std::string{rest};
  }

  const std::string_view base = rest.substr(0, qmark);
  const std::string_view query = rest.substr(qmark + 1);

  std::vector<std::string_view> kept;
  size_t pos = 0;
  while (pos <= query.size()) {
    const auto amp = query.find('&', pos);
    const auto len = (amp == std::string_view::npos ? query.size() : amp) - pos;
    const std::string_view pair = query.substr(pos, len);
    if (!pair.empty()) {
      const auto eq = pair.find('=');
      const std::string_view key = eq == std::string_view::npos ? pair : pair.substr(0, eq);
      if (!is_tracking_param(key)) {
        kept.push_back(pair);
      }
    }
    if (amp == std::string_view::npos) {
      break;
    }
    pos = amp + 1;
  }

  std::string out{base};
  for (size_t i = 0; i < kept.size(); ++i) {
    out += (i == 0 ? '?' : '&');
    out += kept[i];
  }
  return out;
}

Result<std::string> extract_tweet_id(std::string_view raw) {
  const std::string input{trimmed(raw)};
  if (input.empty()) {
    return fail(Error::Kind::BadInput, "empty input");
  }

  if (std::regex_match(input, bare_id_regex())) {
    return input;
  }

  const std::string normalized = normalize_url(input);
  std::smatch match;
  if (!std::regex_search(normalized, match, status_regex())) {
    return fail(Error::Kind::BadInput, "no tweet id found in '" + input + "'");
  }
  return match[1].str();
}

}  // namespace xdl
