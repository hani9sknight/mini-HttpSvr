#ifndef TIME_LSTTIME_
#define TIME_LSTTIME_

#include <time.h>
#include <arpa/inet.h>

#include "logger/logger.h"

class SortedTimerList;
struct ClientData;
class UtilTimer;

struct ClientData
{
    sockaddr_in address_;
    int socket_fd_;
    UtilTimer *timer_;
};

class UtilTimer
{
public:
    UtilTimer() : prev_(nullptr), next_(nullptr){};
    // 失效时间
    time_t expire_time_;
    // 回调函数，负责关闭非活动连接
    void (*cb_func_)(ClientData *);
    ClientData *user_data_;
    UtilTimer *prev_;
    UtilTimer *next_;
};

// 升序的定时器
class SortedTimerList
{
private:
    UtilTimer *head_;
    UtilTimer *tail_;

public:
    SortedTimerList(){};
    ~SortedTimerList()
    {
        UtilTimer *tmp = head_;
        while (tmp)
        {
            head_ = tmp->next_;
            delete tmp;
            tmp = head_;
        }
    };

private:
    void AddTimer(UtilTimer *timer, UtilTimer *list_head)
    {
        UtilTimer *prev = list_head;
        UtilTimer *tmp = prev->next_;
        while (tmp && timer->expire_time_ > tmp->expire_time_)
        {
            prev = tmp;
            tmp = tmp->next_;
        }
        if (tmp)
        {
            prev->next_ = timer;
            timer->next_ = tmp;
            tmp->prev_ = timer;
            timer->prev_ = prev;
        }
        else
        {
            prev->next_ = timer;
            timer->prev_ = prev;
            timer->next_ = nullptr;
            tail_ = timer;
        }
    }

public:
    void AddTimer(UtilTimer *timer)
    {
        if (timer == nullptr)
            return;
        if (head_ == nullptr)
        {
            tail_ = head_ = timer;
            return;
        }
        if (timer->expire_time_ < head_->expire_time_)
        {
            timer->next_ = head_;
            head_->prev_ = timer;
            head_ = timer;
            return;
        }
        AddTimer(timer, head_);
    }

    void AdjustTimer(UtilTimer *timer)
    {
        if (!timer)
            return;
        UtilTimer *tmp = timer->next_;
        if (!tmp || timer->expire_time_ < tmp->expire_time_)
            return;
        if (timer == head_)
        {
            head_ = head_->next_;
            head_->prev_ = nullptr;
            timer->next_ = nullptr;
            AddTimer(timer, head_);
        }
        else
        {
            timer->prev_->next_ = timer->next_;
            timer->next_->prev_ = timer->prev_;
            AddTimer(timer, timer->next_);
        }
    }

    void DeleteTimer(UtilTimer *timer)
    {
        if (!timer)
            return;
        if (timer == head_ && timer == tail_)
        {
            delete timer;
            head_ = tail_ = nullptr;
            return;
        }
        if (timer == head_)
        {
            head_ = head_->next_;
            head_->prev_ = nullptr;
            delete timer;
            return;
        }
        if (timer == tail_)
        {
            tail_ = tail_->prev_;
            tail_->next_ = nullptr;
            delete timer;
            return;
        }
        timer->prev_->next_ = timer->next_;
        timer->next_->prev_ = timer->prev_;
    }

    void Tick()
    {
        if (!head_)
            return;
        Logger *instance = Logger::GetInstance();
        instance->WriteLog(Logger::INFO, "%s", "time tick ");
        instance->Flush();
        time_t cur = time(nullptr);
        UtilTimer *tmp = head_;
        while (tmp)
        {
            if (cur < tmp->expire_time_)
            {
                break;
            }
            tmp->cb_func_(tmp->user_data_);
            head_ = tmp->next_;
            if (head_)
            {
                head_->prev_ = nullptr;
            }
            delete tmp;
            tmp = head_;
        }
    }
};
#endif