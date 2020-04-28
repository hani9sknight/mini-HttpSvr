#ifndef LOCK_LOCKER_
#define LOCK_LOCKER_

#include <mutex>
#include <iostream>
#include <condition_variable>

class Semaphore
{
public:
    Semaphore(int count = 0) : count_(count){};

    void notify()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        ++count_;
        cond_var_.notify_one();
    }

    void wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_.wait(lock,
                       [=]() -> bool { return count_ > 0; });
        --count_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cond_var_;
    int64_t count_;
};

#endif
