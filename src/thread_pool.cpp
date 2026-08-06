#include "xdl/thread_pool.hpp"

#include <algorithm>

namespace xdl {

ThreadPool::ThreadPool(unsigned workers) {
  const unsigned count = std::max(1u, workers);
  workers_.reserve(count);
  for (unsigned i = 0; i < count; ++i) {
    workers_.emplace_back([this] { worker_loop(); });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

void ThreadPool::worker_loop() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });

      // Drain whatever is queued before shutting down, so work already
      // submitted still runs and its futures become ready.
      if (queue_.empty()) {
        return;
      }
      task = std::move(queue_.front());
      queue_.pop();
    }
    // packaged_task stores any escaping exception in its future, so a throwing
    // task cannot take down the worker.
    task();
  }
}

}  // namespace xdl
