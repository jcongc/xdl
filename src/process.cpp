#include "xdl/process.hpp"

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

extern char** environ;

namespace xdl {
namespace {

// Closes a file descriptor exactly once, including on the error paths.
class Fd {
 public:
  Fd() = default;
  explicit Fd(int fd) : fd_(fd) {}
  ~Fd() { reset(); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  Fd(Fd&& other) noexcept : fd_(other.release()) {}
  Fd& operator=(Fd&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.release();
    }
    return *this;
  }

  int get() const { return fd_; }
  bool valid() const { return fd_ >= 0; }
  int release() {
    const int fd = fd_;
    fd_ = -1;
    return fd;
  }
  void reset() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

 private:
  int fd_{-1};
};

struct Pipe {
  Fd read;
  Fd write;
};

bool make_pipe(Pipe& out) {
  std::array<int, 2> fds{};
  if (::pipe(fds.data()) != 0) {
    return false;
  }
  out.read = Fd{fds[0]};
  out.write = Fd{fds[1]};
  return true;
}

}  // namespace

bool program_exists(const std::string& program) {
  if (program.find('/') != std::string::npos) {
    std::error_code ec;
    return std::filesystem::exists(program, ec);
  }

  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return false;
  }

  const std::string path{path_env};
  size_t pos = 0;
  while (pos <= path.size()) {
    const auto colon = path.find(':', pos);
    const auto len = (colon == std::string::npos ? path.size() : colon) - pos;
    if (len > 0) {
      const std::filesystem::path candidate =
          std::filesystem::path{path.substr(pos, len)} / program;
      if (::access(candidate.c_str(), X_OK) == 0) {
        return true;
      }
    }
    if (colon == std::string::npos) {
      break;
    }
    pos = colon + 1;
  }
  return false;
}

Result<ProcessResult> PosixProcessRunner::run(std::span<const std::string> argv,
                                              std::chrono::milliseconds timeout) {
  if (argv.empty()) {
    return fail(Error::Kind::Subprocess, "empty argv");
  }

  Pipe out_pipe;
  Pipe err_pipe;
  if (!make_pipe(out_pipe) || !make_pipe(err_pipe)) {
    return fail(Error::Kind::Io, std::string{"pipe: "} + std::strerror(errno));
  }

  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    return fail(Error::Kind::Subprocess, "posix_spawn_file_actions_init failed");
  }
  posix_spawn_file_actions_adddup2(&actions, out_pipe.write.get(), STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, err_pipe.write.get(), STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, out_pipe.read.get());
  posix_spawn_file_actions_addclose(&actions, err_pipe.read.get());

  // posix_spawn wants a NUL-terminated char* array; the strings stay alive in
  // `argv` for the duration of the call.
  std::vector<char*> raw;
  raw.reserve(argv.size() + 1);
  for (const auto& arg : argv) {
    raw.push_back(const_cast<char*>(arg.c_str()));
  }
  raw.push_back(nullptr);

  pid_t pid = 0;
  // No shell anywhere in this path, so URLs and filenames cannot inject.
  const int spawn_rc = posix_spawnp(&pid, raw[0], &actions, nullptr, raw.data(), environ);
  posix_spawn_file_actions_destroy(&actions);

  if (spawn_rc != 0) {
    return fail(Error::Kind::Subprocess,
                argv[0] + ": " + std::strerror(spawn_rc));
  }

  // The parent must drop its copies of the write ends, or the reads below
  // never see EOF.
  out_pipe.write.reset();
  err_pipe.write.reset();

  ProcessResult result;
  std::array<pollfd, 2> fds{
      pollfd{out_pipe.read.get(), POLLIN, 0},
      pollfd{err_pipe.read.get(), POLLIN, 0},
  };
  std::array<std::string*, 2> sinks{&result.out, &result.err};
  std::array<bool, 2> open_streams{true, true};

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  bool timed_out = false;

  // Both streams are drained together. Draining them one after the other
  // deadlocks as soon as a child fills the pipe buffer of the stream we are
  // not currently reading — yt-dlp's JSON dump is far past that threshold.
  while (open_streams[0] || open_streams[1]) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      timed_out = true;
      break;
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

    for (size_t i = 0; i < fds.size(); ++i) {
      fds[i].fd = open_streams[i] ? fds[i].fd : -1;
      fds[i].revents = 0;
    }

    const int ready = ::poll(fds.data(), fds.size(), static_cast<int>(remaining));
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      return fail(Error::Kind::Io, std::string{"poll: "} + std::strerror(errno));
    }
    if (ready == 0) {
      timed_out = true;
      break;
    }

    for (size_t i = 0; i < fds.size(); ++i) {
      if (!open_streams[i] || fds[i].revents == 0) {
        continue;
      }
      std::array<char, 65536> buffer{};
      const ssize_t n = ::read(fds[i].fd, buffer.data(), buffer.size());
      if (n > 0) {
        sinks[i]->append(buffer.data(), static_cast<size_t>(n));
      } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) {
        open_streams[i] = false;
      }
    }
  }

  if (timed_out) {
    ::kill(pid, SIGKILL);
  }

  int status = 0;
  while (::waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      return fail(Error::Kind::Subprocess,
                  std::string{"waitpid: "} + std::strerror(errno));
    }
  }

  if (timed_out) {
    return fail(Error::Kind::Subprocess, argv[0] + ": timed out");
  }

  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  }
  return result;
}

}  // namespace xdl
