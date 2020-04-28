#ifndef CGI_MYSQLCONNECTPOLL_
#define CGI_MYSQLCONNECTPOLL_

// C standard header
#include <error.h>

// Cpp standard header
#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include <list>
#include <mutex>
#include <atomic>

// Other dependencies
#include <mysql/mysql.h>

// Header in this project
#include "semaphore/semaphore.h"

// 用std::atomic实现的单例模式mysql连接池
class ConnectPool
{
    typedef std::string string;
    typedef std::lock_guard<std::mutex> lock;

public:
    // 连接到数据库
    MYSQL *GetConnetion();
    // 释放连接
    bool ReleaseConnection(MYSQL *connection);
    // 销毁连接池
    void Destory();
    // 获取当前可用连接数
    uint GetFreeConnection() {return free_connection_;};
    // 用单例模式获取连接
    static ConnectPool *GetInstance(
        string url,
        string user,
        string pass_word,
        string dataname,
        uint port,
        uint max_connection);

    // 禁用默认函数
    ConnectPool() = delete;
    ~ConnectPool();
    ConnectPool(const ConnectPool &) = delete;
    ConnectPool &operator=(const ConnectPool &) = delete;
    ConnectPool(ConnectPool &&) = delete;
    ConnectPool& operator= (ConnectPool &&) = delete;


private:
    // 数据成员
    string url_;
    uint port_;
    string user_;
    string pass_word_;
    string data_base_name_;

    // 最大连接数
    uint max_connection_; 
    // 当前连接数
    uint current_connection_;
    // 当前可用连接数
    uint free_connection_;

    static std::mutex mutex_s;
    //采用原子操作实现的单例模式实例
    static std::atomic<ConnectPool*> instance_s;

    // 连接池
    std::list<MYSQL *> connection_list_;
    MYSQL *connection_; // ??????

    ConnectPool(
        string url,
        string user,
        string pass_word,
        string data_base_name,
        uint port,
        uint max_connection);
};

#endif