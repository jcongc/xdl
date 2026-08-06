#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "xdl/error.hpp"

namespace xdl {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct HttpResponse {
  long status{0};
  std::string body;
};

// The first of two seams. Everything that would otherwise reach the network
// goes through here, so tests can substitute a fake and run the whole pipeline
// offline.
class HttpClient {
 public:
  virtual ~HttpClient() = default;

  virtual Result<HttpResponse> get(std::string_view url,
                                   const Headers& headers,
                                   std::chrono::milliseconds timeout) = 0;

  // Writes to `dest` + ".part" and renames on success, so an interrupted run
  // never leaves a truncated file that a later run would mistake for complete.
  virtual Result<void> download(std::string_view url,
                                const std::filesystem::path& dest,
                                std::chrono::milliseconds timeout) = 0;
};

// curl_global_init is not thread-safe and must run before any easy handle is
// created. One of these lives in main, constructed before the thread pool.
class CurlGlobal {
 public:
  CurlGlobal();
  ~CurlGlobal();
  CurlGlobal(const CurlGlobal&) = delete;
  CurlGlobal& operator=(const CurlGlobal&) = delete;
  CurlGlobal(CurlGlobal&&) = delete;
  CurlGlobal& operator=(CurlGlobal&&) = delete;
};

// Owns one CURL* easy handle. Handles are never shared between threads, so
// each worker constructs its own client.
class CurlHttpClient final : public HttpClient {
 public:
  CurlHttpClient();
  ~CurlHttpClient() override;
  CurlHttpClient(const CurlHttpClient&) = delete;
  CurlHttpClient& operator=(const CurlHttpClient&) = delete;

  Result<HttpResponse> get(std::string_view url,
                           const Headers& headers,
                           std::chrono::milliseconds timeout) override;

  Result<void> download(std::string_view url,
                        const std::filesystem::path& dest,
                        std::chrono::milliseconds timeout) override;

 private:
  void* handle_{nullptr};  // CURL*, opaque here to keep curl out of the header
};

// The User-Agent the syndication endpoint expects to see.
extern const char* const kUserAgent;

}  // namespace xdl
