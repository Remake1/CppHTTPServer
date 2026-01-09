#include "./ThreadPool.hpp"


ThreadPool::ThreadPool(size_t threadCount) {
    // Create worker threads
    for (size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                // Unlock a task queue and wait for tasks
                {
                    // Acquire lock
                    std::unique_lock<std::mutex> lock(queue_mutex_);

                    // Wait until there is a task or we are stopping
                    cv_.wait(lock, [this] {
                        return !tasks_.empty() || stop_;
                    });

                    // If stopping and no tasks left, exit
                    if (stop_ && tasks_.empty()) {
                        return;
                    }

                    // Get the next task
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }

                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (std::thread &worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_) {
            throw std::runtime_error("ThreadPool stopped");
        }
        tasks_.emplace(std::move(task));
    }
    cv_.notify_one();
}
