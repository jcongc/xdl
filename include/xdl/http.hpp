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

// Safe to share across threads: libcurl forbids using one easy handle from
// more than one thread at a time, so this type holds no handle of its own and
// instead borrows a thread-local one per call. A single instance can therefore
// be handed to every worker in the pool.
class CurlHttpClient final : public HttpClient {
 public:
  CurlHttpClient() = default;

  Result<HttpResponse> get(std::string_view url,
                           const Headers& headers,
                           std::chrono::milliseconds timeout) override;

  Result<void> download(std::string_view url,
                        const std::filesystem::path& dest,
                        std::chrono::milliseconds timeout) override;
};

// The User-Agent the syndication endpoint expects to see.
extern const char* const kUserAgent;

}  // namespace xdl
