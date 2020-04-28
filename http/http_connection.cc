#include <map>
#include <fstream>
#include <mysql/mysql.h>

#include "http_connection.h"
#include "logger/logger.h"
// 配置文件
#include "config.inc"
#include "root_path.inc"

namespace
{
// 状态短语和描述
const char OK_200_TITLE[] = "OK";
const char ERROR_400_TITLE[] = "Bad Request";
const char ERROR_400_FORM[] = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char ERROR_403_TITLE[] = "Forbidden";
const char ERROR_403_FORM[] = "You do not have permission to get file form this server.\n";
const char ERROR_404_TITLE[] = "Not Found";
const char ERROR_404_FORM[] = "The requested file was not found on this server.\n";
const char ERROR_500_TITLE[] = "Internal Error";
const char ERROR_500_FORM[] = "There was an unusual problem serving the request file.\n";
// html和资源文件路径
const char doc_root[] = ROOT_PATH;
std::map<std::string, std::string> users;
std::mutex users_locker;
} // namespace

#ifdef SYNSQL

void HttpConnection::InitMysqlResult(ConnectPool *conn_pool)
{
    MYSQL *mysql = conn_pool->GetConnetion();

    if (mysql_query(mysql, "SELECT username, passwd FROM user"))
    {
        LOG_ERROR("SELECT error: %s\n", mysql_error(mysql));
    }

    MYSQL_RES *result = mysql_store_result(mysql);
    int fields_num = mysql_num_fields(result);
    MYSQL_FIELD *fields = mysql_fetch_field(result);

    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        // name
        users[row[0]] = row[1];
    }
    conn_pool->ReleaseConnection(mysql);
}

#endif

#ifdef CGISQLPOOL

void HttpConnection::InitResultFile(ConnectPool *conn_pool)
{
    std::ofstream log_file("./cgi/password.inc");
    MYSQL *mysql = conn_pool->GetConnetion();
    if (mysql_query(mysql, "SELECT username, passwd FROM user"))
    {
        LOG_ERROR("SELECT error: %s\n", mysql_error(mysql));
    }

    MYSQL_RES *result = mysql_store_result(mysql);
    int fields_num = mysql_num_fields(result);
    MYSQL_FIELD *fields = mysql_fetch_field(result);

    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        std::string tmp1(row[0]), tmp2(row[1]);
        log_file << row[0] << " " << row[0] << "\n";
        users[tmp1] = tmp2;
    }
    conn_pool->ReleaseConnection(mysql);
}

#endif

int SetNonBlock(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void AddFd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
#ifdef ET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef LT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif
    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    SetNonBlock(fd);
}

void RemoveFd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}
// 重置EPOLLONESHOT
void ModFd(int epollfd, int fd, int ev_event)
{
    epoll_event event;
    event.data.fd = fd;
#ifdef ET
    event.events = ev_event | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif
#ifdef LT
    event.events = ev_event | EPOLLONESHOT | EPOLLRDHUP;
#endif
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int HttpConnection::user_count_ = 0;
int HttpConnection::epoll_fd_ = -1;

// 关闭连接
void HttpConnection::CloseConnection(bool real_close)
{
    if (real_close && (socket_fd_ != -1))
    {
        RemoveFd(epoll_fd_, socket_fd_);
        socket_fd_ = -1;
        --user_count_;
    }
}

void HttpConnection::Initialize(int socket_fd, const sockaddr_in &addr)
{
    socket_fd_ = socket_fd;
    address_ = addr;

    AddFd(epoll_fd_, socket_fd_, true);
    ++user_count_;
    Initialize();
}

void HttpConnection::Initialize()
{
    mysql_ = nullptr;
    bytes_to_send_ = 0;
    bytes_have_send_ = 0;
    check_state_ = CHECK_STATE_REQUESTLINE;
    linger_ = false;
    method_ = GET;
    url_ = nullptr;
    http_version_ = nullptr;
    content_length_ = 0;
    host_ = nullptr;
    start_line_ = 0;
    checked_idx_ = 0;
    read_idx_ = 0;
    write_idx_ = 0;
    cgi_ = 0;
    memset(read_buffer_, '\0', READ_BUFFER_SIZE);
    memset(write_buffer_, '\0', WRITE_BUFFER_SIZE);
    memset(real_file_, '\0', FILNAME_LEN);
}

// 从状态机。负责处理读取缓冲区，将"\r\n"变为"\0\0"，并按读到的字符返回已经读到的状态
HttpConnection::LineStatus HttpConnection::PraseLine()
{
    char tmp;
    for (; checked_idx_ < read_idx_; ++checked_idx_)
    {
        tmp = read_buffer_[checked_idx_];
        if (tmp == '\r')
        {
            if ((checked_idx_ + 1) == read_idx_)
            {
                // 字符'\r'位于缓冲区结尾，表示接受不完整，需要继续读取
                return LINE_OPEN;
            }
            else if (read_buffer_[checked_idx_ + 1] == '\n')
            {
                // 读取到了"\r\n"表示读到了请求一行的结尾，将其改为"\0\0",并返回该行状态正常
                read_buffer_[checked_idx_++] = '\0';
                read_buffer_[checked_idx_++] = '\0';
                return LINE_OK;
            }
            // '\r'后既不是缓冲区结尾，也不是’\n‘，表示请求格式错误
            return LINE_BAD;
        }
        else if (tmp == '\n')
        {
            if (checked_idx_ > 1 && read_buffer_[checked_idx_ - 1] == '\r')
            {
                // 读取到了"\r\n"表示读到了请求一行的结尾，将其改为"\0\0",并返回该行状态正常
                read_buffer_[checked_idx_ - 1] = '\0';
                read_buffer_[checked_idx_++] = '\0';
                return LINE_OK;
            }
            // 请求格式错误
            return LINE_BAD;
        }
    }
    // 没有检测到"\r\n"，表示还应该继续读取
    return LINE_OPEN;
}

bool HttpConnection::ReadOnce()
{
    if (read_idx_ >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;
    while (true)
    {
        bytes_read = recv(socket_fd_,
                          read_buffer_ + read_idx_,
                          READ_BUFFER_SIZE - read_idx_,
                          0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return false;
        }
        else if (bytes_read == 0)
            return false;
        read_idx_ += bytes_read;
    }
    return true;
}
// 解析请求行，并将方法、url、版本号填入对应成员变量
HttpConnection::HttpCode HttpConnection::ParseRequestLine(char *text)
{
    url_ = strpbrk(text, " \t");
    if (url_ == nullptr)
    {
        return BAD_REQUEST;
    }
    *url_++ = '\0';
    char *method = text;
    // 确定请求方法
    if (strcasecmp(method, "GET") == 0)
    {
        method_ = GET;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        method_ = POST;
        cgi_ = 1;
    }
    else
    {
        return BAD_REQUEST;
    }
    // 跳过空格，生成真正的url
    url_ += strspn(url_, " \t");
    // 用生成url的方法生成http版本号
    http_version_ = strpbrk(url_, " \t");
    if (!http_version_)
        return BAD_REQUEST;
    *http_version_++ = '\0';
    http_version_ += strspn(http_version_, " \t");

    if (strcasecmp(http_version_, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    // 判断是https还是http，并更改url以跳过
    if (strncasecmp(url_, "http://", 7) == 0)
    {
        url_ += 7;
        url_ = strchr(url_, '/');
    }
    else if (strncasecmp(url_, "https://", 8) == 0)
    {
        url_ += 8;
        url_ = strchr(url_, '/');
    }

    if (url_ == nullptr || url_[0] != '/')
    {
        return BAD_REQUEST;
    }
    // 初始页面
    if (strlen(url_) == 1)
        strcat(url_, "judge.html");
    // 请求行读取完毕，将状态重置为CHECK_STATE_HEADER，下一步解析请求头
    check_state_ = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析请求头，填入对应成员
HttpConnection::HttpCode HttpConnection::ParseHeaders(char *text)
{
    if (text[0] == '\0')
    {
        // 空行，表示请求头结束，下一步该切换主状态机状态以读取正文
        if (content_length_ != 0)
        {
            // 正文长度非零，表示是POST请求，需要继续读取
            check_state_ = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则为GET请求
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        // 跳过空格和'\t'
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            // keep-alive表示为持续链接，linger_字段应设置为True
            linger_ = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        // 解析正文长度
        text += 15;
        text += strspn(text, "\t");
        // 正文长度关系到GET还是POST请求
        content_length_ = atoi(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        host_ = text;
    }
    else
    {
        LOG_INFO("Unknow header: %s", text);
        Logger::GetInstance()->Flush();
    }
    return NO_REQUEST;
}

HttpConnection::HttpCode HttpConnection::ParseContent(char *text)
{
    if (read_idx_ >= (content_length_ + checked_idx_))
    {
        text[content_length_] = '\0';
        string_ = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}
//
HttpConnection::HttpCode HttpConnection::ProcessRead()
{
    LineStatus status = LINE_OK;
    HttpCode ret_code = NO_REQUEST;
    char *text = nullptr;
    /*  两种情况会继续解析请求 :
   ①主状态机正在解析请求正文（只有POST方法才会）
   且从状态机状态正常（GET方法此时已经将状态置为LINE_OPEN） 
   ②从状态机状态正常（主状态机解析请求行和请求头） */
    while ((check_state_ == CHECK_STATE_CONTENT && status == LINE_OK) ||
           (status = PraseLine()) == LINE_OK)
    {
        text = GetLine();
        start_line_ = checked_idx_;
        LOG_INFO("%s", text);
        Logger::GetInstance()->Flush();
        switch (check_state_)
        {
        // 解析请求行
        case CHECK_STATE_REQUESTLINE:
        {
            ret_code = ParseRequestLine(text);
            if (ret_code == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        // 解析请求头
        case CHECK_STATE_HEADER:
        {
            ret_code = ParseHeaders(text);
            if (ret_code == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret_code == GET_REQUEST)
            {
                // 在解析到GET请求后，调用DoRequest生成响应
                return DoRequest();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret_code = ParseContent(text);
            if (ret_code == GET_REQUEST)
            {
                // ParseContent返回值为GET_REQUEST表示读取到POST请求，应调用DoRequest生成响应
                return DoRequest();
            }
            // GET请求，解析完正文后为了避免继续循环，要更新状态
            status = LINE_OPEN;
            break;
        }
        default:
        {
            return INTERNAL_ERROR;
        }
        }
    }
    return NO_REQUEST;
}

// 返回写入是否出错，并写入响应
bool HttpConnection::ProcessWrite(HttpCode ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        AddStatusLine(500, ERROR_500_TITLE);
        AddHeader(strlen(ERROR_500_FORM));
        if (!AddContent(ERROR_500_FORM))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        AddStatusLine(404, ERROR_500_TITLE);
        AddHeader(strlen(ERROR_404_FORM));
        if (!AddContent(ERROR_404_FORM))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        AddStatusLine(403, ERROR_500_TITLE);
        AddHeader(strlen(ERROR_403_FORM));
        if (!AddContent(ERROR_403_FORM))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        AddStatusLine(200, OK_200_TITLE);
        if (file_stat_.st_size != 0)
        {
            AddHeader(file_stat_.st_size);
            iv_[0].iov_base = write_buffer_;
            iv_[1].iov_base = file_address_;
            iv_[0].iov_len = write_idx_;
            iv_[1].iov_len = file_stat_.st_size;
            iv_count_ = 2;
            bytes_to_send_ = write_idx_ + file_stat_.st_size;
            return true;
        }
        else
        {
            const char ok_string[] = "<html><body></body></html>";
            AddHeader(strlen(ok_string));
            if (!AddContent(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    iv_[0].iov_base = write_buffer_;
    iv_[0].iov_len = write_idx_;
    iv_count_ = 1;
    bytes_to_send_ = write_idx_;
    return true;
}

HttpConnection::HttpCode HttpConnection::DoRequest()
{
    strcpy(real_file_, doc_root);
    int len = strlen(doc_root);
    // 查找最后一个'/'字符
    const char *p = strrchr(url_, '/');

    // 登录或注册
    if (cgi_ == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        auto real_url = new char[200];
        char flag = url_[1];
        strcpy(real_url, "/");
        strcat(real_url, url_ + 2);
        strncpy(real_file_ + len, real_url, FILNAME_LEN - len - 1);
        delete real_url;

        // 提取用户名、密码
        std::string name, passwd;
        int i, j;
        for (i = 5; string_[i] != '&'; ++i)
            name += string_[i];
        for (i = i + 10; string_[i] != '\0'; ++i, ++j)
            passwd += string_[i];

#ifdef SYNSQL
        // 如果为注册
        if (*(p + 1) == '3')
        {
            std::string inserter = "INSERT INTO user(username, passwd) VALUES('" + name + "', '" + passwd + "')";
            if (users.find(name) == users.end())
            {
                int res = 0;
                {
                    std::lock_guard<std::mutex> locker(users_locker);
                    res = mysql_query(mysql_, inserter.c_str());
                    users.insert({name, passwd});
                }
                if (!res)
                    strcpy(url_, "/log.html");
                else
                {
                    strcpy(url_, "/registerError.html");
                }
            }
            else
            {
                strcpy(url_, "/registerError.html");
            }
        }
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == passwd)
                strcpy(url_, "/welcome.html");
            else
                strcpy(url_, "/logError.html");
        }
#endif

#ifdef CGISQLPOOL
        if (*(p + 1) == '3')
        {
            std::string inserter;
            inserter = "INSERT INTO user(username, passwd) VALUES('" + name + "', '" + passwd + "')";
            if (users.find(name) == users.end())
            {
                {
                    std::lock_guard<std::mutex> locker(users_locker);
                    int res = mysql_query(mysql_, inserter.c_str());
                    users.insert({name, passwd});
                }

                if (res != 0)
                {
                    strcpy(url_, "/log.html");
                    std::ofstream out("./cgi/id_password.inc", std::ios::app);
                    out << name << " " << passwd << "\n";
                }
                else
                    strcpy(url_, "/registerError.html");
            }
            else
                strcpy(url_, "/registerError.html");
        }

        else if (*(p + 1) == '2')
        {
            pid_t pid;
            int pipe_fd[2];
            if (pipe(pipe_fd) < 0)
            {
                LOG_ERROR("pipe error: %d", 4);
                return BAD_REQUEST;
            }
            if ((pid = fork()) < 0)
            {
                LOG_ERROR("fork error: %d", 4);
                return BAD_REQUEST;
            }
            if (pid == 0)
            {
                dup2(pipe_fd[1], 1);
                close(pipe_fd[0]);
                execl(real_file_, name.c_str(), passwd.c_str(), "./cgi/id_password.inc", (char *)nullptr);
            }
            else
            {
                close(pipe_fd[1]);
                char result;
                if (int ret = read(pipe_fd[0], &result, 1) != 1)
                {
                    LOG_ERROR("pipe read error: ret=%d", ret);
                    return BAD_REQUEST;
                }
                LOG_INFO("%s", "Sign in checking");
                Logger::GetInstance()->Flush();
                if (result == '1')
                {
                    strcpy(url_, "/welcome.html");
                }
                else
                    strcpy(url_, "/logError.html");
                waitpid(pid, nullptr, 0);
            }
        }
#endif

#ifdef CGISQL
        pid_t pid;
        int pipe_fd[2];

        if (pipe(pipe_fd) == -1)
        {
            LOG_ERROR("pipe() error:%d", 4);
            return BAD_REQUEST;
        }
        if ((pid = fork()) < 0)
        {
            LOG_ERROR("fork() error:%d", 3);
            return BAD_REQUEST;
        }

        if (pid == 0)
        {
            dup2(pipe_fd[1], 1);
            close(pipe_fd[0]);
            execl(real_file_, &flag, name.c_str(), passwd.c_str(), (char *)nullptr);
        }
        else
        {
            close(pipe_fd[1]);
            char result;
            int ret = read(pipe_fd[0], &result, 1);

            if (ret != 1)
            {
                LOG_ERROR("pipe read error: ret =%d", ret);
                return BAD_REQUEST;
            }
            if (flag == '2')
            {
                LOG_INFO("%s", "Sign in checking");
                Logger::GetInstance()->Flush();
                if (result == '1')
                {
                    strcpy(url_, "welcon.html");
                }
                else
                {
                    strcpy(url_, "/logError.html");
                }
            }
            else if (flag == '3')
            {
                LOG_INFO("%s", "Sign up checking");
                Logger::GetInstance()->Flush();
                if (result == '1')
                    strcpy(url_, "/log.html");
                else
                    strcpy(url_, "/registerError.html");
            }
            waitpid(pid, NULL, 0);
        }

#endif
    }
    // 根据url判断，将所需文件名拼接到root路径下
    const char *path;
    switch (*(p + 1))
    {
    case '0':
        path = "/register.html";
        break;
    case '1':
        path = "/log.html";
        break;
    case '5':
        path = "/picture.html";
        break;
    case '6':
        path = "/video.html";
        break;
    case '7':
        path = "/webbench.html";
        break;
    default:
        path = url_;
        break;
    }

    strncpy(real_file_ + len, path, FILNAME_LEN - len - 1);
    if (stat(real_file_, &file_stat_) < 0)
        return NO_RESOURCE;
    // 判断可否读
    if (!(file_stat_.st_mode & S_IROTH))
    {
        // 不可读返回FORBIDDEN_REQUEST
        return FORBIDDEN_REQUEST;
    }
    if (S_ISDIR(file_stat_.st_mode))
    {
        // 若为该文件为目录则返回BAD_REQUEST
        return BAD_REQUEST;
    }
    // 打开文件，并映射到内存
    int fd = open(real_file_, O_RDONLY);
    file_address_ = (char *)mmap(nullptr, file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void HttpConnection::Unmap()
{
    if (file_address_)
    {
        munmap(file_address_, file_stat_.st_size);
        file_address_ = nullptr;
    }
}

bool HttpConnection::Write()
{
    int tmp = 0, new_add = 0;

    if (bytes_to_send_ == 0)
    {
        ModFd(epoll_fd_, socket_fd_, EPOLLIN);
        Initialize();
        return true;
    }

    while (true)
    {
        tmp = writev(socket_fd_, iv_, iv_count_);
        if (tmp > 0)
        {
            bytes_have_send_ += tmp;
            new_add = bytes_have_send_ - write_idx_;
        }
        else if (tmp == -1)
        {
            if (errno == EAGAIN)
            {
                if (bytes_have_send_ >= iv_[0].iov_len)
                {
                    iv_[0].iov_len = 0;
                    iv_[1].iov_base = file_address_ + new_add;
                    iv_[1].iov_len = bytes_to_send_;
                }
                else
                {
                    iv_[0].iov_base = write_buffer_ + bytes_to_send_;
                    iv_[0].iov_len = iv_[0].iov_len - bytes_have_send_;
                }
                ModFd(epoll_fd_, socket_fd_, EPOLLOUT);
                return true;
            }
            Unmap();
            return false;
        }
        bytes_to_send_ -= tmp;
        if (bytes_to_send_ <= 0)
        {
            Unmap();
            ModFd(epoll_fd_, socket_fd_, EPOLLIN);
            if (linger_)
            {
                Initialize();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

// 将相应报文写入缓冲区的工具函数，并输出到日志文件
bool HttpConnection::AddResponse(const char *format, ...)
{
    if (write_idx_ >= WRITE_BUFFER_SIZE)
        return false;
    va_list valist;
    va_start(valist, format);
    int len = vsnprintf(write_buffer_ + write_idx_,
                        WRITE_BUFFER_SIZE - write_idx_ - 1,
                        format,
                        valist);
    if (len >= (WRITE_BUFFER_SIZE - write_idx_ - 1))
    {
        va_end(valist);
        return false;
    }
    write_idx_ += len;
    va_end(valist);
    LOG_INFO("request:%s", write_buffer_);
    Logger::GetInstance()->Flush();
    return true;
}

void HttpConnection::Process()
{
    HttpCode code = ProcessRead();
    if (code == NO_REQUEST)
    {
        ModFd(epoll_fd_, socket_fd_, EPOLLIN);
        return;
    }
    if (!ProcessWrite(code))
    {
        CloseConnection();
    }
    ModFd(epoll_fd_, socket_fd_, EPOLLOUT);
}
