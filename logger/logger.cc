#include <string.h>
#include <sys/time.h>
#include <stdarg.h>
#include <thread>
#include <ctime>

#include "logger.h"

Logger::Logger() : count_(0), is_async_(false){};

std::mutex Logger::mutex_;
Logger::~Logger()
{
    delete logger_queue_;
};

bool Logger::Initialize(const std::string &file_name,
                        size_t logger_buffer_size,
                        size_t split_lines,
                        size_t max_queue_size)
{
    if (max_queue_size >= 1)
    {
        is_async_ = true;
        logger_queue_ = new BlockQueue<std::string>(max_queue_size);
        std::thread thrd(flush_logger_thread);
    }
    logger_buffer_size_ = logger_buffer_size;
    buffer_ = new char[logger_buffer_size];
    memset(buffer_, 0, logger_buffer_size);
    split_lines_ = split_lines;
    // 查找最后一个'/'字符；
    size_t pos = file_name.rfind('/');
    // 生成时间
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    today_ = my_tm.tm_mday;

    char log_full_name[256] = {0};
    if (pos == std::string::npos)
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name.c_str());
    }
    else
    {
        log_name_ = file_name.substr(pos + 1);
        dir_name_ = file_name.substr(0, pos);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name_.c_str(), my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name_.c_str());
    }

    file_.open(log_full_name, std::ios::app);
    if (file_.rdstate() == std::ios::failbit)
        return false;
    return true;
}

void Logger::WriteLog(LogLevel level, const char *format, ...)
{
    ULock locker(mutex_,std::defer_lock);
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    char s[16] = {0};
    switch (level)
    {
    case DEBUG:
        strcpy(s, "[Debug]:");
        break;
    case INFO:
        strcpy(s, "[Info]:");
        break;
    case WARN:
        strcpy(s, "[Warn]:");
        break;
    case ERROR:
        strcpy(s, "[Error]:");
        break;
    default:
        strcpy(s, "[Info]:");
        break;
    }


    locker.lock();
    ++count_;
    // 当发现当前时间不等于前次日志时间，或者日志行数超过最大行数时需要新建日志
    if(today_ != my_tm.tm_mday || count_% split_lines_ ==0)
    {
        char new_log_name[256] = {0};
        file_.close();
        char tail[16] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        if(today_!= my_tm.tm_mday)
        {
            snprintf(new_log_name, 255, "%s%s%s", dir_name_.c_str(), tail, log_name_.c_str());
            today_ = my_tm.tm_mday;
            count_ = 0;
        }
        else
        {
            snprintf(new_log_name, 255, "%s%s%s.%lu", dir_name_.c_str(), tail, log_name_.c_str(), count_ / split_lines_);
        }

        file_.open(new_log_name, std::ios::app);
    }

    locker.unlock();

    va_list valist;
    va_start(valist, format);

    std::string log_str;

    // 写入具体时间
    locker.lock();
    size_t n = snprintf(buffer_, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                    my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                    my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    size_t m = vsnprintf(buffer_+n, logger_buffer_size_-n-1,format, valist);
    buffer_[m+n] = '\n';
    buffer_[m+n+1] = '\0';
    log_str.assign(buffer_);
    locker.unlock();

    // 如果为异步就把log_str加到阻塞队列中
    // 如果为同步则加锁写入。
    if(is_async_)
    {
        logger_queue_->Push(log_str);
    }
    else
    {
        locker.lock();
        file_<<log_str;
        locker.unlock();
    }
    va_end(valist);
}

void Logger::Flush(void)
{
    Lock locker(mutex_);
    file_.flush();
}