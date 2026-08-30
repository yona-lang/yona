#ifndef YONA_SUPPORT_THREADPOOL_H
#define YONA_SUPPORT_THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace yona::runtime::async {

/// Owns a fixed worker set and a FIFO of submitted callables.
///
/// submit(), pending_tasks(), and the atomic observations are safe to call from
/// multiple threads while the pool is running. shutdown() and destruction must
/// be serialized with submit() and with object lifetime. Shutdown drains work
/// already accepted before joining the workers.
class ThreadPool {
private:
  std::vector<std::thread> workers;
  std::queue<std::function<void()>> tasks;
  mutable std::mutex queue_mutex;
  std::condition_variable cv;
  std::atomic<bool> stop{false};
  std::atomic<std::size_t> active_tasks{0};

  // Worker thread function
  void worker_thread();

public:
  // Create thread pool with specified number of threads (default: hardware
  // concurrency)
  explicit ThreadPool(std::size_t num_threads = 0);
  ~ThreadPool();

  /// Transfer an owned callable to the queue.
  ///
  /// Throws std::runtime_error after shutdown starts. Exceptions escaping a
  /// plain task are caught by the worker and reported to stderr.
  void submit(std::function<void()> task);

  /// Transfer an owned callable and return its single-owner result future.
  /// Task exceptions are delivered through the future. Submission failure is
  /// thrown synchronously and no future is returned.
  template <typename T> std::future<T> submit_async(std::function<T()> task) {
    auto promise = std::make_shared<std::promise<T>>();
    auto future = promise->get_future();

    submit([promise, task]() {
      try {
        if constexpr (std::is_void_v<T>) {
          task();
          promise->set_value();
        } else {
          promise->set_value(task());
        }
      } catch (...) {
        promise->set_exception(std::current_exception());
      }
    });

    return future;
  }

  // Get the number of worker threads
  std::size_t thread_count() const { return workers.size(); }

  // Get the number of pending tasks
  std::size_t pending_tasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return tasks.size();
  }

  // Get the number of active tasks
  std::size_t get_active_tasks() const { return active_tasks.load(); }

  /// Wait until both the queue and active-task count are empty. Accepted tasks
  /// may enqueue more work before this condition is reached.
  void wait_all();

  /// Stop accepting work, drain accepted tasks, and join owned workers.
  /// Repeated calls are harmless, but concurrent control calls are unsupported.
  void shutdown();

  // Check if the pool is shutting down
  bool is_stopping() const { return stop.load(); }
};

/// Mutex-protected owning deque used by the work-stealing pool.
///
/// Successful pop and steal operations move ownership into the output object.
template <typename T> class WorkStealingQueue {
private:
  std::deque<T> queue;
  mutable std::mutex mtx;

public:
  void push(T item) {
    std::lock_guard<std::mutex> lock(mtx);
    queue.push_back(std::move(item));
  }

  bool try_pop(T &item) {
    std::lock_guard<std::mutex> lock(mtx);
    if (queue.empty())
      return false;
    item = std::move(queue.front());
    queue.pop_front();
    return true;
  }

  bool try_steal(T &item) {
    std::lock_guard<std::mutex> lock(mtx);
    if (queue.empty())
      return false;
    item = std::move(queue.back());
    queue.pop_back();
    return true;
  }

  bool empty() const {
    std::lock_guard<std::mutex> lock(mtx);
    return queue.empty();
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(mtx);
    return queue.size();
  }
};

/// Owns worker-local queues and distributes callables by work stealing.
///
/// Submission transfers the callable and throws std::runtime_error after
/// shutdown starts. Shutdown may abandon work still queued, and exceptions
/// escaping tasks are not converted into an error result. Concurrent submit()
/// calls are safe while running; shutdown and destruction must be serialized
/// with submission and object lifetime.
class WorkStealingThreadPool {
private:
  struct WorkerThread {
    std::thread thread;
    std::unique_ptr<WorkStealingQueue<std::function<void()>>> local_queue;

    WorkerThread()
        : local_queue(
              std::make_unique<WorkStealingQueue<std::function<void()>>>()) {}
  };

  std::vector<std::unique_ptr<WorkerThread>> workers;
  WorkStealingQueue<std::function<void()>> global_queue;
  std::atomic<bool> stop{false};
  std::atomic<size_t> next_worker{0};

  void worker_thread(size_t worker_id);

public:
  explicit WorkStealingThreadPool(size_t num_threads = 0);
  ~WorkStealingThreadPool();

  void submit(std::function<void()> task);
  void shutdown();
};

} // namespace yona::runtime::async

#endif /* YONA_SUPPORT_THREADPOOL_H */
