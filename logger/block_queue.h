/*************************************************************
*采用std::deque实现的阻塞队列                                 
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/

#ifndef LOGGER_BLOCKQUEUE_
#define LOGGER_BLOCKQUEUE_

#include <sys/time.h>

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <deque>
//#include <memory>
#include <chrono>

// 阻塞队列,
template <class T>
class BlockQueue
{
    typedef std::lock_guard<std::mutex> Lock;
    typedef std::unique_lock<std::mutex> ULock;

public:
    BlockQueue(size_t max_size) : container_(max_size), max_size_(max_size){};

    // 判断队列是否已满
    bool Full() const
    {
        Lock locker(mutex_);
        return container_.size() >= max_size_;
    }

    // 判断队列是否为空
    bool Empty() const
    {
        Lock locker(mutex_);
        return container_.size() == 0;
    }

    // 返回队首元素，需要在使用前判断是否为空
    const T &Front() const
    {
        Lock locker(mutex_);
        return container_.front();
    }

    const T &Back() const
    {
        Lock locker(mutex_);
        return container_.back();
    }

    bool Push(const T &item)
    {
        ULock locker(mutex_);

        if (container_.size() >= max_size_)
        {
            condi_var_.notify_all();
            return false;
        }
        else
        {
            container_.push_back(std::move(item));
            condi_var_.notify_all();
            return true;
        }
    }

public:
    bool Pop(T &item)
    {
        ULock locker(mutex_);
        condi_var_.wait(locker, [this](){return !this->container_.empty();});
        item = container_.front();
        container_.pop_front();
        return true;
    }

    // 以毫秒记的时间限制
    bool Pop(T& item, int ms)
    {
        // 毫秒类型
        using MicroSeconds = std::chrono::microseconds; 
        // std::cv_status定义的枚举变量
        auto time_out = std::cv_status::timeout;
        // 以毫秒记时间限制
        std::chrono::microseconds time_limit(ms);
        ULock locker(mutex_);
        if(container_.empty())
        {
            // 超时返回false
            if(condi_var_.wait_for(locker, time_limit) == time_out)
            {
                return false;
            }
        }

        item = container_.front();
        container_.pop_front();
        return true;
    }


private:
    std::deque<T> container_;
    size_t max_size_;
    std::mutex mutex_;
    std::condition_variable condi_var_;
};

#endif