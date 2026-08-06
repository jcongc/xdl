#include "xdl/http.hpp"

#include <curl/curl.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace xdl {

const char* const kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36";

namespace {

size_t append_to_string(char* data, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  const size_t total = size * nmemb;
  out->append(data, total);
  return total;
}

size_t append_to_file(char* data, size_t size, size_t nmemb, void* userdata) {
  auto* file = static_cast<std::FILE*>(userdata);
  return std::fwrite(data, size, nmemb, file) * size;
}

// Closes the FILE* exactly once, including when a transfer fails midway.
class FileHandle {
 public:
  explicit FileHandle(const std::filesystem::path& path)
      : file_(std::fopen(path.c_str(), "wb")) {}
  ~FileHandle() { reset(); }
  FileHandle(const FileHandle&) = delete;
  FileHandle& operator=(const FileHandle&) = delete;

  std::FILE* get() const { return file_; }
  bool valid() const { return file_ != nullptr; }
  void reset() {
    if (file_ != nullptr) {
      std::fclose(file_);
      file_ = nullptr;
    }
  }

 private:
  std::FILE* file_{nullptr};
};

CURL* as_curl(void* handle) { return static_cast<CURL*>(handle); }

void apply_common_options(CURL* curl, std::string_view url,
                          std::chrono::milliseconds timeout) {
  const std::string url_str{url};
  curl_easy_setopt(curl, CURLOPT_URL, url_str.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout.count()));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                   static_cast<long>(timeout.count()));
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
}

}  // namespace

CurlGlobal::CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
CurlGlobal::~CurlGlobal() { curl_global_cleanup(); }

CurlHttpClient::CurlHttpClient() : handle_(curl_easy_init()) {}

CurlHttpClient::~CurlHttpClient() {
  if (handle_ != nullptr) {
    curl_easy_cleanup(as_curl(handle_));
  }
}

Result<HttpResponse> CurlHttpClient::get(std::string_view url,
                                         const Headers& headers,
                                         std::chrono::milliseconds timeout) {
  if (handle_ == nullptr) {
    return fail(Error::Kind::Network, "curl handle unavailable");
  }

  CURL* curl = as_curl(handle_);
  curl_easy_reset(curl);
  apply_common_options(curl, url, timeout);

  std::string body;
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_to_string);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

  curl_slist* list = nullptr;
  for (const auto& [name, value] : headers) {
    list = curl_slist_append(list, (name + ": " + value).c_str());
  }
  if (list != nullptr) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
  }

  const CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (list != nullptr) {
    curl_slist_free_all(list);
  }

  if (rc != CURLE_OK) {
    return fail(Error::Kind::Network, curl_easy_strerror(rc));
  }
  return HttpResponse{status, std::move(body)};
}

Result<void> CurlHttpClient::download(std::string_view url,
                                      const std::filesystem::path& dest,
                                      std::chrono::milliseconds timeout) {
  if (handle_ == nullptr) {
    return fail(Error::Kind::Network, "curl handle unavailable");
  }

  std::error_code ec;
  std::filesystem::create_directories(dest.parent_path(), ec);

  // Write to a sibling .part file and rename on success, so an interrupted run
  // never leaves behind a truncated file that a later run treats as complete.
  const auto partial = std::filesystem::path{dest.string() + ".part"};
  FileHandle file{partial};
  if (!file.valid()) {
    return fail(Error::Kind::Io, "cannot open " + partial.string());
  }

  CURL* curl = as_curl(handle_);
  curl_easy_reset(curl);
  apply_common_options(curl, url, timeout);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_to_file);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, file.get());

  const CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  file.reset();

  if (rc != CURLE_OK) {
    std::filesystem::remove(partial, ec);
    return fail(Error::Kind::Network, curl_easy_strerror(rc));
  }
  if (status >= 400) {
    std::filesystem::remove(partial, ec);
    return fail(Error::Kind::Network,
                "HTTP " + std::to_string(status) + " for " + std::string{url});
  }

  std::filesystem::rename(partial, dest, ec);
  if (ec) {
    std::filesystem::remove(partial, ec);
    return fail(Error::Kind::Io, "rename failed: " + ec.message());
  }
  return {};
}

}  // namespace xdl
