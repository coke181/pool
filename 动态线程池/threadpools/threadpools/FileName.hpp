#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <cassert>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bpool {
    class ThreadPool
    {
    public:
        using MutexGuard = std::lock_guard<std::mutex>;
        using UniqueLock = std::unique_lock<std::mutex>;
        using Thread = std::thread;
        using ThreadID = std::thread::id;
        using Task = std::function<void()>;

        ThreadPool()
            : ThreadPool(Thread::hardware_concurrency())
        {
        }
        explicit ThreadPool(size_t maxThreads)
            : quit_(false),
            currentThreads_(0),
            idleThreads_(0),
            maxThreads_(maxThreads)
        {
            for (size_t i = 0; i < maxThreads_; ++i) {
                threads_.emplace_back([this] {
                    while (true) {
                        Task task;
                        {
                            UniqueLock lock(mutex_);
                            cv_.wait(lock, [this] { return quit_ || !tasks_.empty(); });
                            if (quit_ && tasks_.empty()) return;
                            task = std::move(tasks_.front());
                            tasks_.pop();
                        }
                        task();
                    }
                });
            }
        }
        ThreadPool(const ThreadPool &) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        ~ThreadPool()
        {
            {
                MutexGuard guard(mutex_);
                quit_ = true;
            }
            cv_.notify_all();

            for (auto& t : threads_)
            {
                if (t.joinable())
                    t.join();
            }
        }

        template<class F, class... Args>
        auto submit(F&& f, Args&&... args)
            -> std::future<typename std::result_of<F(Args...)>::type>
        {
            using return_type = typename std::result_of<F(Args...)>::type;
            auto task = std::make_shared<std::packaged_task<return_type()>>(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );
            std::future<return_type> res = task->get_future();
            {
                MutexGuard lock(mutex_);
                if (quit_)
                    throw std::runtime_error("submit on stopped ThreadPool");
                tasks_.emplace([task]() { (*task)(); });
            }
            cv_.notify_one();
            return res;
        }

        size_t threadsNum() const {
            return threads_.size();
        }

    private:
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::vector<std::thread> threads_;
        std::queue<Task> tasks_;
        bool quit_;
        size_t currentThreads_;
        size_t idleThreads_;
        size_t maxThreads_;
    };
}

#endif // THREADPOOL_H