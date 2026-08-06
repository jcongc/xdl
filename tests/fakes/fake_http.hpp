#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "xdl/http.hpp"

namespace xdl::testing {

// Serves canned responses keyed by a substring of the requested URL, so tests
// never touch the network. Records every call for ordering assertions.
class FakeHttpClient final : public HttpClient {
 public:
  struct Canned {
    HttpResponse response;
    std::optional<Error> error;
  };

  void on_get(std::string url_substring, HttpResponse response) {
    gets_.push_back({std::move(url_substring), Canned{std::move(response), std::nullopt}});
  }

  void fail_get(std::string url_substring, Error error) {
    gets_.push_back({std::move(url_substring), Canned{HttpResponse{}, std::move(error)}});
  }

  void fail_download(Error error) { download_error_ = std::move(error); }

  // Bytes written by download(); lets tests assert on file size.
  void set_payload(std::string payload) { payload_ = std::move(payload); }

  Result<HttpResponse> get(std::string_view url, const Headers&,
                           std::chrono::milliseconds) override {
    requested_urls.emplace_back(url);
    for (const auto& [needle, canned] : gets_) {
      if (url.find(needle) != std::string_view::npos) {
        if (canned.error) {
          return std::unexpected(*canned.error);
        }
        return canned.response;
      }
    }
    return fail(Error::Kind::Network, "FakeHttpClient: no canned response for " +
                                          std::string{url});
  }

  Result<void> download(std::string_view url, const std::filesystem::path& dest,
                        std::chrono::milliseconds) override {
    downloaded.emplace_back(url, dest);
    if (download_error_) {
      return std::unexpected(*download_error_);
    }
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    std::ofstream out(dest, std::ios::binary);
    if (!out) {
      return fail(Error::Kind::Io, "FakeHttpClient: cannot write " + dest.string());
    }
    out << payload_;
    return {};
  }

  std::vector<std::string> requested_urls;
  std::vector<std::pair<std::string, std::filesystem::path>> downloaded;

 private:
  std::vector<std::pair<std::string, Canned>> gets_;
  std::optional<Error> download_error_;
  std::string payload_{"fake-mp4-bytes"};
};

}  // namespace xdl::testing
