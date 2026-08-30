#include "yona/Support/ThreadPool.h"

#include <iostream>

namespace yona::runtime::async {

namespace chrono = std::chrono;
namespace this_thread = std::this_thread;
using std::cerr;
using std::endl;
using std::exception;
using std::function;
using std::lock_guard;
using std::make_unique;
using std::mutex;
using std::runtime_error;
using std::size_t;
using std::thread;
using std::unique_lock;

ThreadPool::ThreadPool(size_t num_threads) {
  if (num_threads == 0) {
    num_threads = thread::hardware_concurrency();
    if (num_threads == 0) {
      num_threads = 4; // Default fallback
    }
  }

  workers.reserve(num_threads);
  for (size_t i = 0; i < num_threads; ++i) {
    workers.emplace_back(&ThreadPool::worker_thread, this);
  }
}

ThreadPool::~ThreadPool() { shutdown(); }

void ThreadPool::worker_thread() {
  while (true) {
    function<void()> task;

    {
      unique_lock<mutex> lock(queue_mutex);
      cv.wait(lock, [this] { return stop.load() || !tasks.empty(); });

      if (stop.load() && tasks.empty()) {
        return;
      }

      if (!tasks.empty()) {
        task = std::move(tasks.front());
        tasks.pop();
        active_tasks++;
      }
    }

    if (task) {
      try {
        task();
      } catch (const exception &e) {
        cerr << "Exception in thread pool task: " << e.what() << endl;
      } catch (...) {
        cerr << "Unknown exception in thread pool task" << endl;
      }
      active_tasks--;
    }
  }
}

void ThreadPool::submit(function<void()> task) {
  {
    lock_guard<mutex> lock(queue_mutex);
    if (stop.load()) {
      throw runtime_error("Cannot submit task to stopped thread pool");
    }
    tasks.push(std::move(task));
  }
  cv.notify_one();
}

void ThreadPool::wait_all() {
  while (true) {
    {
      lock_guard<mutex> lock(queue_mutex);
      if (tasks.empty() && active_tasks.load() == 0) {
        break;
      }
    }
    this_thread::sleep_for(chrono::milliseconds(10));
  }
}

void ThreadPool::shutdown() {
  stop.store(true);
  cv.notify_all();

  for (auto &worker : workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

// Work-stealing thread pool implementation

WorkStealingThreadPool::WorkStealingThreadPool(size_t num_threads) {
  if (num_threads == 0) {
    num_threads = thread::hardware_concurrency();
    if (num_threads == 0) {
      num_threads = 4; // Default fallback
    }
  }

  workers.reserve(num_threads);
  for (size_t i = 0; i < num_threads; ++i) {
    workers.push_back(make_unique<WorkerThread>());
    workers.back()->thread =
        thread(&WorkStealingThreadPool::worker_thread, this, i);
  }
}

WorkStealingThreadPool::~WorkStealingThreadPool() { shutdown(); }

void WorkStealingThreadPool::worker_thread(size_t worker_id) {
  auto &my_queue = workers[worker_id]->local_queue;

  while (!stop.load()) {
    function<void()> task;

    // Try to get task from local queue first
    if (my_queue->try_pop(task)) {
      task();
      continue;
    }

    // Try to get task from global queue
    if (global_queue.try_pop(task)) {
      task();
      continue;
    }

    // Try to steal from other workers
    bool found = false;
    for (size_t i = 0; i < workers.size(); ++i) {
      if (i != worker_id && workers[i]->local_queue->try_steal(task)) {
        task();
        found = true;
        break;
      }
    }

    if (!found) {
      // No work available, yield
      this_thread::yield();
    }
  }
}

void WorkStealingThreadPool::submit(function<void()> task) {
  if (stop.load()) {
    throw runtime_error("Cannot submit task to stopped thread pool");
  }

  // Round-robin distribution to worker queues
  size_t worker_id = next_worker.fetch_add(1) % workers.size();
  workers[worker_id]->local_queue->push(std::move(task));
}

void WorkStealingThreadPool::shutdown() {
  stop.store(true);

  for (auto &worker : workers) {
    if (worker->thread.joinable()) {
      worker->thread.join();
    }
  }
}

} // namespace yona::runtime::async
