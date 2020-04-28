#ifndef LOGGER_LOGGER_
#define LOGGER_LOGGER_

#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>

#include <iostream>
#include <string>
#include <fstream>
//#include <thread>

#include "block_queue.h"

class Logger
{
    typedef std::lock_guard<std::mutex> Lock;
    typedef std::unique_lock<std::mutex> ULock;
private:
    Logger();
    virtual ~Logger();
    void async_write_log()
    {
        std::string single_log;
        // 从阻塞队列中取出一个字符串，写入文件
        while (logger_queue_->Pop(single_log))
        {
            file_ << single_log;
        }
    }

    // 路径名
    std::string dir_name_;
    // log文件名
    std::string log_name_;
    // 日志最大行数
    size_t split_lines_;
    // 日志缓冲区大小
    size_t logger_buffer_size_;
    // 日志行数记录
    size_t count_;
    // 记录当天是哪一天
    size_t today_;
    // 用于输出日志的文件输出流
    std::ofstream file_;
    char *buffer_;
    // 阻塞队列
    BlockQueue<std::string> *logger_queue_;
    // 是否同步
    bool is_async_;
    static std::mutex mutex_;

public:
    enum LogLevel
    {
        DEBUG,
        INFO,
        WARN,
        ERROR
    };
    // 采用局部静态对象实现的单例
    static Logger *GetInstance()
    {
        Lock locker(mutex_);
        static Logger instance;
        return &instance;
    }

    static void flush_logger_thread()
    {
        Logger::GetInstance()->async_write_log();
    }
    // 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool Initialize(const std::string &file_name,
                    size_t logger_buffer_size = 8192,
                    size_t split_lines = 5000000,
                    size_t max_queue_size = 0);

    void WriteLog(LogLevel level, const char *format, ...);

    void Flush(void);
};

#define LOG_DEBUG(format, ...) Logger::GetInstance()->WriteLog(Logger::DEBUG, format, __VA_ARGS__)
#define LOG_INFO(format, ...) Logger::GetInstance()->WriteLog(Logger::INFO, format, __VA_ARGS__)
#define LOG_WARN(format, ...) Logger::GetInstance()->WriteLog(Logger::WARN, format, __VA_ARGS__)
#define LOG_ERROR(format, ...) Logger::GetInstance()->WriteLog(Logger::ERROR, format, __VA_ARGS__)

#endif