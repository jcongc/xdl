#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace xdl {

// A fixed-size worker pool, the direct analogue of Python's
// ThreadPoolExecutor. Tasks are std::packaged_task, so an exception escaping a
// task is captured in its future rather than terminating the process.
class ThreadPool {
 public:
  explicit ThreadPool(unsigned workers);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template <class F>
  auto submit(F&& fn) -> std::future<std::invoke_result_t<F>> {
    using R = std::invoke_result_t<F>;
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
    std::future<R> fut = task->get_future();
    {
      std::lock_guard lock(mutex_);
      queue_.emplace([task] { (*task)(); });
    }
    cv_.notify_one();
    return fut;
  }

 private:
  void worker_loop();

  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stopping_{false};
};

}  // namespace xdl
