#include "xdl/thread_pool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {

TEST(ThreadPool, RunsEverySubmittedTask) {
  xdl::ThreadPool pool{4};
  std::vector<std::future<int>> futures;
  futures.reserve(100);
  for (int i = 0; i < 100; ++i) {
    futures.push_back(pool.submit([i] { return i * 2; }));
  }

  int sum = 0;
  for (auto& future : futures) {
    sum += future.get();
  }
  EXPECT_EQ(sum, 9900);
}

TEST(ThreadPool, ResultsAreIndependentOfCompletionOrder) {
  xdl::ThreadPool pool{8};
  std::vector<std::future<int>> futures;
  for (int i = 0; i < 50; ++i) {
    // Later tasks finish sooner, so completion order differs from submission.
    futures.push_back(pool.submit([i] {
      std::this_thread::sleep_for(std::chrono::milliseconds{(50 - i) % 5});
      return i;
    }));
  }
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(futures[static_cast<size_t>(i)].get(), i);
  }
}

// A throwing task must land in its own future rather than tearing down the
// worker — one bad tweet cannot be allowed to kill a batch.
TEST(ThreadPool, ContainsExceptionsWithinTheirFuture) {
  xdl::ThreadPool pool{2};
  auto bad = pool.submit([]() -> int { throw std::runtime_error{"boom"}; });
  auto good = pool.submit([] { return 7; });

  EXPECT_THROW(bad.get(), std::runtime_error);
  EXPECT_EQ(good.get(), 7) << "the pool must still be alive after a throwing task";
}

TEST(ThreadPool, DrainsQueuedWorkBeforeShuttingDown) {
  std::atomic<int> completed{0};
  {
    xdl::ThreadPool pool{2};
    for (int i = 0; i < 32; ++i) {
      pool.submit([&completed] {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
        completed.fetch_add(1);
      });
    }
  }  // destructor joins
  EXPECT_EQ(completed.load(), 32);
}

TEST(ThreadPool, SupportsVoidReturningTasks) {
  xdl::ThreadPool pool{1};
  std::atomic<bool> ran{false};
  auto future = pool.submit([&ran] { ran.store(true); });
  future.get();
  EXPECT_TRUE(ran.load());
}

TEST(ThreadPool, ClampsWorkerCountToAtLeastOne) {
  xdl::ThreadPool pool{0};
  EXPECT_EQ(pool.submit([] { return 42; }).get(), 42);
}

}  // namespace
