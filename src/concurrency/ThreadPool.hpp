#ifndef CPPHTTPSERVER_THREADPOOL_HPP
#define CPPHTTPSERVER_THREADPOOL_HPP

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>


class ThreadPool {
public:
    explicit ThreadPool(size_t threadCount);
    ~ThreadPool();

    void submit(std::function<void()> task);

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()> > tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;

    // Flag to stop the pool
    bool stop_ = false;
};


#endif //CPPHTTPSERVER_THREADPOOL_HPP