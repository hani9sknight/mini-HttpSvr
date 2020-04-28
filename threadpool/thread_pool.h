#ifndef THREADPOOL_THREADPOOL_
#define THREADPOOL_THREADPOOL_

#include <pthread.h>

#include <cstdio>
#include <list>
#include <exception>
#include <vector>

#include "semaphore/semaphore.h"
#include "cgi/mysql_connect_pool.h"
#include "config.inc"

template <class Request>
class ThreadPool
{
    typedef std::lock_guard<std::mutex> Lock;
    typedef std::unique_lock<std::mutex> ULock;

public:
    ThreadPool(ConnectPool *conn_pool, size_t thread_num = 16, size_t max_request = MAX_EVENT_NUMBER);
    ~ThreadPool();
    // 向工作队列添加任务
    bool Append(Request *request);

private:
    // 负责取出工作队列中的任务，并执行
    static void *Worker(void *arg);
    void Run();

    // 线程池中的线程数
    size_t thread_number_;
    // 请求队列中允许的最大请求数
    size_t max_requests_;
    // 线程池数组
    pthread_t *threads_;
    // 请求队列
    std::list<Request *> work_queue_;
    // 用于保护线程池的锁
    std::mutex mutex_;
    // 表征是否有任务需要处理的信号量
    Semaphore queue_state_;
    // 是否结束线程
    bool stop_;
    // 数据库连接池
    ConnectPool *sql_conn_pool_;
};

template <class Request>
ThreadPool<Request>::ThreadPool(ConnectPool *conn_pool,
                                size_t thread_number,
                                size_t max_request)
    : thread_number_(thread_number),
      max_requests_(max_request),
      stop_(false), sql_conn_pool_(conn_pool),
      queue_state_(0)
{
    if (thread_number <= 0 || max_request <= 0)
    {
        throw std::exception();
    }
    threads_ = new pthread_t[thread_number_];
    for (size_t i = 0; i < thread_number_; ++i)
    {
        if (pthread_create(threads_ + i, nullptr, Worker, this) != 0)
        {
            delete[] threads_;
            throw std::exception();
        }
        if (pthread_detach(threads_[i]))
        {
            delete[] threads_;
            throw std::exception();
        }
    }
}

template <class Request>
ThreadPool<Request>::~ThreadPool()
{
    delete[] threads_;
    stop_ = true;
}

// 将事务添加进工作队列中，成功返回true；若队列长度太长，则添加失败，返回false
template <class Request>
bool ThreadPool<Request>::Append(Request *request)
{
    {
        Lock locker(mutex_);
        if (work_queue_.size() > max_requests_)
        {
            return false;
        }
        work_queue_.push_back(request);
    }

    queue_state_.notify();
    return true;
}

template <class Request>
void *ThreadPool<Request>::Worker(void *arg)
{
    ThreadPool<Request> *pool = static_cast<ThreadPool<Request> *>(arg);
    pool->Run();
    return pool;
}

// stop_为真表示线程池已被析构
template <class Request>
void ThreadPool<Request>::Run()
{
    while (!stop_)
    {
        // 从线程池中取出线程，信号量-1
        Request *request;
        queue_state_.wait();
        {
            Lock locker(mutex_);
            if (work_queue_.empty())
            {
                continue;
            }
            request = work_queue_.front();
            work_queue_.pop_front();
        }
        if (request == nullptr)
            continue;
        // 从连接池中取出一个连接
        request->mysql_ = sql_conn_pool_->GetConnetion();
        // 处理请求
        request->Process();
        // 归还连接
        sql_conn_pool_->ReleaseConnection(request->mysql_);
    }
}

#endif