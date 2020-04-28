// Self header
#include "mysql_connect_pool.h"

// Cpp standard header
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <list>
#include <mutex>
#include <atomic>
#include <thread>
#include <iostream>

// Other dependencies
#include <mysql/mysql.h>

std::atomic<ConnectPool *> ConnectPool::instance_s;
std::mutex ConnectPool::mutex_s;

// 私有构造函数
ConnectPool::ConnectPool(
    string url,
    string user,
    string pass_word,
    string data_base_name,
    uint port,
    uint max_connection) : url_(url), user_(user),
                           pass_word_(pass_word), port_(port),
                           data_base_name_(data_base_name)
{
    for (uint i = 0; i < max_connection; ++i)
    {
        MYSQL *sql_connection = mysql_init(nullptr);
        if (sql_connection == nullptr)
        {
            std::cout << "Error: " << mysql_error(sql_connection) << "\n";
            std::exit(1);
        }
        sql_connection = mysql_real_connect(sql_connection, url.c_str(),
                                            user_.c_str(), pass_word.c_str(),
                                            data_base_name.c_str(), port,
                                            nullptr, 0);
        if (sql_connection == nullptr)
        {
            std::cout << "Error: " << mysql_error(sql_connection) << "\n";
            std::exit(1);
        }
        this->connection_list_.push_back(sql_connection);
        ++free_connection_;
    }
};

// 用std::atomic实现的单例模式
ConnectPool *ConnectPool::GetInstance(
    string url,
    string user,
    string pass_word,
    string dataname,
    uint port,
    uint max_connection)
{
    ConnectPool *tmp = instance_s.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire); // 获取内存栅栏
    if (tmp == nullptr)
    {
        lock locker(mutex_s);
        tmp = instance_s.load(std::memory_order_relaxed);
        if (tmp == nullptr)
        {
            tmp = new ConnectPool(url, user, pass_word, dataname, port, max_connection);
            std::atomic_thread_fence(std::memory_order_release); // 释放内存栅栏
            instance_s.store(tmp, std::memory_order_relaxed);
        }
    }
    return tmp;
}

// 从连接池中返回一个可用连接
MYSQL *ConnectPool::GetConnetion()
{
    MYSQL *sql_connection = nullptr;
    lock locker(mutex_s);
    if (!connection_list_.empty())
    {
        sql_connection = connection_list_.front();
        connection_list_.pop_front();
        ++current_connection_;
        --free_connection_;
    }
    return sql_connection;
}

// 释放当前连接
bool ConnectPool::ReleaseConnection(MYSQL *connection)
{
    lock locker(mutex_s);
    if (connection != nullptr)
    {
        ++free_connection_;
        --current_connection_;
        connection_list_.push_back(connection);
        return true;
    }
    return false;
}

// 销毁连接池
void ConnectPool::Destory()
{
    lock locker(mutex_s);
    while (!connection_list_.empty())
        ;
    {
        MYSQL *sql_connection = connection_list_.front();
        mysql_close(sql_connection);
        connection_list_.pop_front();
    }
    current_connection_ = 0;
    free_connection_ = 0;
}

ConnectPool::~ConnectPool()
{
    Destory();
}
