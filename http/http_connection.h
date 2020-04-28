#ifndef HTTP_HTTPCONNECTION_H
#define HTTP_HTTPCONNECTION_H

#include <unistd.h>
#include <csignal>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include "cgi/mysql_connect_pool.h"

// 设置非阻塞
int SetNonBlock(int fd);
// 以注册事件，设置one_shot决定是否开启EPOLLONESHOT
void AddFd(int epollfd, int fd, bool one_shot);
// 移除epollfd的兴趣列表中移除fd
void RemoveFd(int epollfd, int fd);

class HttpConnection
{
public:
    static const int FILNAME_LEN = 200,
                     READ_BUFFER_SIZE = 2048,
                     WRITE_BUFFER_SIZE = 1024;
    static int epoll_fd_;
    static int user_count_;
    MYSQL *mysql_;
    // 请求的方法
    enum Method
    {
        GET,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    // 主状态机状态
    enum CheckState
    {
        CHECK_STATE_REQUESTLINE,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    // 报文解析结果
    enum HttpCode
    {
        NO_REQUEST,        // 请求不完整，需要继续读取
        GET_REQUEST,       // 获取请求，随后会调用DoRequest()生成响应
        BAD_REQUEST,       // Http语法有误
        NO_RESOURCE,       // 资源不存在
        FORBIDDEN_REQUEST, // 请求资源禁止访问，没有读取权限
        FILE_REQUEST,      // 请求资源可以访问，调用Process_write()完成响应
        INTERNAL_ERROR,    // 服务器内部出错
        CLOSED_CONNECTION  // 链接关闭（未使用）
    };
    // 从状态机状态
    enum LineStatus
    {
        LINE_OK,
        LINE_BAD,
        LINE_OPEN
    };

    HttpConnection(){};
    ~HttpConnection(){};

public:
    // 初始化socket地址
    void Initialize(int socket_fd, const sockaddr_in &addr);
    // 断开Http连接
    void CloseConnection(bool real_close = true);
    // 调用其他成员函数，执行读取请求和生成响应的任务，最后关闭连接
    void Process();
    // 循环读取socket中的数据，直到无数据可读或者对端关闭连接
    bool ReadOnce();
    // 写入响应报文
    bool Write();
    // 返回地址信息
    const sockaddr_in *GetAddress() { return &address_; };
    // 同步线程初始化数据库读取表
    static void InitMysqlResult(ConnectPool *conn_pool);
    // CGI线程池初始化数据库
    static void InitResultFile(ConnectPool *conn_pool);

private:
    void Initialize();
    // 从读缓冲区读取并处理请求
    HttpCode ProcessRead();
    // 向写缓冲区写入响应
    bool ProcessWrite(HttpCode ret);
    // 解析请求行
    HttpCode ParseRequestLine(char *text);
    // 解析请求头
    HttpCode ParseHeaders(char *text);
    // 解析请求正文，仅POST请求会调用
    HttpCode ParseContent(char *text);
    // 生成响应
    HttpCode DoRequest();
    // 用于偏移指针，指向未处理的行的第一个字符
    char *GetLine() { return read_buffer_ + start_line_; };
    // 从状态机解析一行，返回改行是请求的那个部分
    LineStatus PraseLine();

    void Unmap();
    // 生成响应的8个部分
    bool AddResponse(const char *format, ...);
    bool AddContent(const char *content)
    {
        return AddResponse("%s", content);
    }
    bool AddStatusLine(int status, const char *title)
    {
        return AddResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
    };
    bool AddHeader(int content_length)
    {
        AddContentLength(content_length);
        AddLinger();
        AddContentType();
        AddBlankLine();
    }
    bool AddContentType()
    {
        return AddResponse("Content-Type:%s\r\n", "text/html");
    }
    bool AddContentLength(int content_length)
    {
        return AddResponse("Content-Length: %d\r\n", content_length);
    }
    bool AddLinger()
    {
        return AddResponse("Connection: %s\r\n", linger_ ? "keep-alive" : "close");
    }
    bool AddBlankLine()
    {
        return AddResponse("%s", "\r\n");
    }

private:
    int socket_fd_;
    // 读缓冲区中数据最后一字节的下一个位置
    int read_idx_, write_idx_, checked_idx_;
    // 读缓冲区中一个数据行的起始位置
    int start_line_;
    // 正文长度
    int content_length_;
    // 是否持续连接
    bool linger_;
    // 请求的html文件名和读、写缓冲区
    char real_file_[FILNAME_LEN],
        read_buffer_[READ_BUFFER_SIZE],
        write_buffer_[WRITE_BUFFER_SIZE];

    char *url_;
    char *http_version_;
    char *host_;
    // 读取服务器上的文件地址
    char *file_address_;

    struct stat file_stat_;
    struct iovec iv_[2];
    int iv_count_;
    // 是否启用POST
    int cgi_;
    // 请求正文信息
    char *string_;
    int bytes_to_send_;
    int bytes_have_send_;
    // 从状态机的状态，表示在读缓冲区中读取的位置
    CheckState check_state_;
    // 请求方法
    Method method_;
    // 请求地址
    struct sockaddr_in address_;
};

#endif