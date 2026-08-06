#include "xdl/process.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using xdl::Error;
using xdl::PosixProcessRunner;

std::chrono::milliseconds timeout() { return std::chrono::milliseconds{10000}; }

TEST(PosixProcessRunner, CapturesStdoutAndExitCode) {
  PosixProcessRunner runner;
  const std::vector<std::string> argv{"/bin/echo", "hello"};

  auto result = runner.run(argv, timeout());
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->exit_code, 0);
  EXPECT_EQ(result->out, "hello\n");
  EXPECT_TRUE(result->err.empty());
}

TEST(PosixProcessRunner, CapturesStderrSeparately) {
  PosixProcessRunner runner;
  const std::vector<std::string> argv{"/bin/sh", "-c", "echo out; echo err >&2"};

  auto result = runner.run(argv, timeout());
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->out, "out\n");
  EXPECT_EQ(result->err, "err\n");
}

TEST(PosixProcessRunner, ReportsNonZeroExitCodes) {
  PosixProcessRunner runner;
  const std::vector<std::string> argv{"/bin/sh", "-c", "exit 3"};

  auto result = runner.run(argv, timeout());
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->exit_code, 3);
}

// Draining stdout and stderr one after the other deadlocks the moment a child
// fills the pipe buffer of the stream not being read. yt-dlp's JSON dump is
// well past that threshold, so both must be polled together.
TEST(PosixProcessRunner, DoesNotDeadlockOnLargeInterleavedOutput) {
  PosixProcessRunner runner;
  const std::vector<std::string> argv{
      "/bin/sh", "-c",
      "for i in $(seq 1 400); do "
      "  head -c 1024 /dev/zero | tr '\\0' 'a'; "
      "  head -c 1024 /dev/zero | tr '\\0' 'b' >&2; "
      "done"};

  auto result = runner.run(argv, timeout());
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->out.size(), 400u * 1024u);
  EXPECT_EQ(result->err.size(), 400u * 1024u);
}

TEST(PosixProcessRunner, ReportsMissingBinaries) {
  PosixProcessRunner runner;
  const std::vector<std::string> argv{"definitely-not-a-real-binary-xdl", "--version"};

  auto result = runner.run(argv, timeout());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, Error::Kind::Subprocess);
}

TEST(PosixProcessRunner, RejectsEmptyArgv) {
  PosixProcessRunner runner;
  auto result = runner.run(std::vector<std::string>{}, timeout());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, Error::Kind::Subprocess);
}

TEST(PosixProcessRunner, KillsProcessesThatOverrunTheTimeout) {
  PosixProcessRunner runner;
  const std::vector<std::string> argv{"/bin/sh", "-c", "sleep 30"};

  const auto began = std::chrono::steady_clock::now();
  auto result = runner.run(argv, std::chrono::milliseconds{300});
  const auto elapsed = std::chrono::steady_clock::now() - began;

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, Error::Kind::Subprocess);
  EXPECT_LT(elapsed, std::chrono::seconds{5}) << "timeout did not kill the child";
}

// Arguments are passed as an explicit argv vector and never through a shell,
// so shell metacharacters are inert data.
TEST(PosixProcessRunner, DoesNotInterpretShellMetacharacters) {
  PosixProcessRunner runner;
  const std::vector<std::string> argv{"/bin/echo", "; rm -rf /tmp/nope; $(whoami)"};

  auto result = runner.run(argv, timeout());
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->out, "; rm -rf /tmp/nope; $(whoami)\n");
}

TEST(ProgramExists, FindsBinariesOnPath) {
  EXPECT_TRUE(xdl::program_exists("sh"));
  EXPECT_FALSE(xdl::program_exists("definitely-not-a-real-binary-xdl"));
}

TEST(ProgramExists, HandlesAbsolutePaths) {
  EXPECT_TRUE(xdl::program_exists("/bin/sh"));
  EXPECT_FALSE(xdl::program_exists("/bin/definitely-not-real"));
}

}  // namespace
