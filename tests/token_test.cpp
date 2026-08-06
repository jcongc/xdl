#include "xdl/token.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>

namespace {

// Captured from the reference Python implementation, which is known to produce
// tokens the live syndication endpoint accepts. If these drift, every network
// call fails in a way that looks like a server problem rather than a maths
// problem — so they are pinned here deliberately.
TEST(SyndicationToken, MatchesReferenceImplementation) {
  const std::pair<std::string_view, std::string_view> vectors[] = {
      {"1491475671058681863", "3m5lxayrcgmflrnsfko6r"},
      {"746487912313688067", "1t55skvm2q9xz1l23ejpb9"},
      {"2085077475109769243", "51ygpgs8i883itbro1or"},
      {"123", "138spvehogpno1ukz"},
      {"20", "6dq1a2xwd93jfti"},
  };

  for (const auto& [id, expected] : vectors) {
    EXPECT_EQ(xdl::syndication_token(id), expected) << "tweet id " << id;
  }
}

TEST(SyndicationToken, ContainsNoZerosOrDots) {
  // The JS does .replace(/(0+|\.)/g, ''), which removes every '0' anywhere in
  // the string, not merely leading or trailing runs.
  const auto token = xdl::syndication_token("1491475671058681863");
  EXPECT_EQ(token.find('0'), std::string::npos);
  EXPECT_EQ(token.find('.'), std::string::npos);
}

TEST(SyndicationToken, ReturnsEmptyForNonNumericInput) {
  EXPECT_TRUE(xdl::syndication_token("not-a-number").empty());
}

}  // namespace
