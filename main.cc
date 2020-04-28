#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>

#include <cassert>
#include <vector>

#include "threadpool/thread_pool.h"
#include "time/lst_time.h"
#include "http/http_connection.h"
#include "logger/logger.h"
#include "cgi/mysql_connect_pool.h"

#include "config.inc"

namespace
{
int pipefd[2];
int epoll_fd = 0;
SortedTimerList time_list;
} // namespace

void SigalHandler(int sig)
{
    // 为确保可重入，拷贝信号到局部变量
    int former_errno = errno;
    int former_sig = sig;
    // 通过管道发送信号，这里这样做是为了尽量减少信号处理函数的长度，
    // 避免因为处理时间过长而使其他信号被抛弃，仅仅将信号转发给主程序做处理
    send(pipefd[1], (char *)&former_sig, 1, 0);
    errno = former_errno;
}

void AddSig(int sig, void (*handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}
// 刷新计时队列，再次出发SIGALRM信号
void TimerHandler()
{
    time_list.Tick();
    alarm(TIMESLOT);
}

// 计时队列超时时调用的回调函数，负责删除非活动连接,解除epoll注册
void cb_func(ClientData *user_data)
{
    assert(user_data);
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, user_data->socket_fd_, nullptr);
    close(user_data->socket_fd_);
    HttpConnection::user_count_--;
    LOG_INFO("Close fd %d", user_data->socket_fd_);
    Logger::GetInstance()->Flush();
}

void ShowError(int conn_fd, const char *info)
{
    printf("%s", info);
    send(conn_fd, info, strlen(info), 0);
    close(conn_fd);
}

int main(int argc, char *argv[])
{

#ifdef ASYNLOG
    Logger::GetInstance()->Initialize("./mylog.log", 8192, 2000000, 10);

#endif

#ifdef SYNLOG
    Logger::GetInstance()->Initialize("./mylog.log", 8192, 2000000, 0);
#endif
    if (argc <= 1)
    {
        printf("Usage: %s port_number\n", basename(argv[0]));
        return 1;
    }
    // 忽略SIGPIPE
    AddSig(SIGPIPE, SIG_IGN);
    int port = atoi(argv[1]);
    // 创建连接池，单例模式
    ConnectPool *conn_pool = ConnectPool::GetInstance(HOST, MYSQL_USR,
                                                      MYSQL_PASSWD, SQL_NAME,
                                                      MYSQL_PORT, MAX_CONNECTION);
    auto pool = new ThreadPool<HttpConnection>(conn_pool);
    auto users = new HttpConnection[MAX_FD];

    int user_count = 0;
// 初始化数据库读取表
#ifdef SYNSQL
    HttpConnection::InitMysqlResult(conn_pool);
#endif

#ifdef CGISQLPOOL
    HttpConnection::InitResultFile(conn_pool);
#endif

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);
    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    int flag = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    int ret = bind(listen_fd, (struct sockaddr *)&address, sizeof(address));
    assert(ret == 0);
    assert(listen(listen_fd, 5) >= 0);
    epoll_event event[MAX_EVENT_NUMBER];
    int epoll_fd = epoll_create(5);
    assert(epoll_fd != -1);

    AddFd(epoll_fd, listen_fd, false);
    HttpConnection::epoll_fd_ = epoll_fd;

    assert(socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd) == 0);
    // 为了不让信号处理函数时间太长，因此这里只能把管道的写端设置为非阻塞
    SetNonBlock(pipefd[1]);
    AddFd(epoll_fd, pipefd[0], false);
    AddSig(SIGALRM, SigalHandler, false);
    AddSig(SIGTERM, SigalHandler, false);
    // 循环条件
    bool stop_server = false;
    auto user_timer = new ClientData[MAX_FD];
    bool time_out = false;
    alarm(TIMESLOT);
    while (!stop_server)
    {
        int event_num = epoll_wait(epoll_fd, event, MAX_EVENT_NUMBER, -1);
        if (event_num < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < event_num; ++i)
        {
            int sock_fd = event[i].data.fd;
            // 收到新连接请求
            if (sock_fd == listen_fd)
            {
                struct sockaddr_in client_address;
                socklen_t client_length;
#ifdef LT
                int conn_fd = accept(listen_fd, (struct sockaddr *)&client_address, &client_length);
                if (conn_fd < 0)
                {
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }

                if (HttpConnection::user_count_ >= MAX_FD)
                {
                    ShowError(conn_fd, "Internal server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }
                users[conn_fd].Initialize(conn_fd, client_address);

                user_timer[conn_fd].address_ = client_address;
                user_timer[conn_fd].socket_fd_ = conn_fd;
                UtilTimer *timer = new UtilTimer;
                timer->cb_func_ = cb_func;
                timer->user_data_ = &user_timer[conn_fd];
                timer->expire_time_ = time(nullptr) + 3 * TIMESLOT;
                user_timer[conn_fd].timer_ = timer;
                time_list.AddTimer(timer);
#endif

#ifdef ET
                // ET模式需要一次性读完数据
                while (true)
                {
                    int conn_fd = accept(listen_fd, (sockaddr *)&client_address, &client_length);
                    if (conn_fd < 0)
                    {
                        LOG_ERROR("accept error, errno is : %d", errno);
                        break;
                    }
                    if (HttpConnection::user_count_ >= MAX_FD)
                    {
                        ShowError(conn_fd, "Internal server busy");
                        LOG_ERROR("%s", "Internal server busy");
                        break;
                    }
                    users[conn_fd].Initialize(conn_fd, client_address);

                    user_timer[conn_fd].address_ = client_address;
                    user_timer[conn_fd].socket_fd_ = conn_fd;
                    UtilTimer *timer = new UtilTimer;
                    timer->cb_func_ = cb_func;
                    timer->user_data_ = &user_timer[conn_fd];
                    timer->expire_time_ = time(nullptr) + 3 * TIMESLOT;
                    user_timer[conn_fd].timer_ = timer;
                    time_list.AddTimer(timer);
                }
                continue;
#endif
            }
            else if (event[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                cb_func(&user_timer[sock_fd]);
                UtilTimer *timer = user_timer[sock_fd].timer_;
                if (timer)
                {
                    time_list.DeleteTimer(timer);
                }
            }
            else if (sock_fd == pipefd[0] && (event[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                // 此处管道读端的信号只可能是SIGALRM和SIGTREM，即14或15
                int ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; ++i)
                    {
                        if (signals[i] == SIGALRM)
                        {
                            time_out = true;
                            break;
                        }
                        else if (signals[i] == SIGTERM)
                        {
                            stop_server = true;
                        }
                    }
                }
            }
            else if (event[i].events & EPOLLIN)
            {
                UtilTimer *timer = user_timer[sock_fd].timer_;
                if (users[sock_fd].ReadOnce())
                {
                    LOG_INFO("deal with client(%s)", inet_ntoa(users[sock_fd].GetAddress()->sin_addr));
                    Logger::GetInstance()->Flush();
                    pool->Append(users + sock_fd);
                    if (timer)
                    {
                        timer->expire_time_ = time(nullptr) + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust time once");
                        Logger::GetInstance()->Flush();
                        time_list.AdjustTimer(timer);
                    }
                }
                else
                {
                    timer->cb_func_(&user_timer[sock_fd]);
                    if (timer)
                    {
                        time_list.DeleteTimer(timer);
                    }
                }
            }
            else if (event[i].events & EPOLLOUT)
            {
                UtilTimer *timer = user_timer[sock_fd].timer_;
                if (users[sock_fd].Write())
                {
                    LOG_INFO("send data to the client(%s)", inet_ntoa(users[sock_fd].GetAddress()->sin_addr));
                    Logger::GetInstance()->Flush();
                    if (timer)
                    {
                        timer->expire_time_ += time(nullptr) + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust time once");
                        Logger::GetInstance()->Flush();
                        time_list.AdjustTimer(timer);
                    }
                }
                else
                {
                    timer->cb_func_(&user_timer[sock_fd]);
                    if (timer)
                    {
                        time_list.DeleteTimer(timer);
                    }
                }
            }
        }
        // 完成读写后再处理超时连接
        if (time_out)
        {
            TimerHandler();
            time_out = false;
        }
    }

    close(epoll_fd);
    close(listen_fd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete pool;
    delete[] user_timer;
    conn_pool->Destory();
    return 0;
}