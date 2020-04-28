#include <string.h>

#include <iostream>
#include <cstdio>
#include <map>
#include <fstream>
#include <sstream>

#include "mysql_connect_pool.h"

#include "config.inc"

int main(int argc, char *argv[])
{
    std::map<std::string, std::string> users;

#ifdef CGISQL
    MYSQL *con = mysql_init(nullptr);
    if (con == nullptr)
    {
        std::cout << "Error: " << mysql_error(con);
        exit(1);
    }
    con = mysql_real_connect(con, HOST, MYSQL_USR, MYSQL_PASSWD,
                             SQL_NAME, MYSQL_PORT, nullptr, 0);
    if (con == nullptr)
    {
        std::cout << "Error: " << mysql_error(con);
        exit(1);
    }

    if (mysql_query(con, "SELECT username,passwd FROM user"))
    {
        std::cout << "INSERT error: " << mysql_error(con) << "\n";
        exit(1);
    }

    MYSQL_RES *result = mysql_store_result(con);
    int num_field = mysql_num_fields(result);
    MYSQL_FIELD *fields = mysql_fetch_field(result);

    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        users[row[0]] = row[1];
    }

    std::string name(argv[1]), passwd(argv[2]);
    char flag = *argv[0];
    std::string cmd = "INSERT INTO user(username, passwd) VALUES('" +
                      name + "', '" + passwd + "')";
    if (flag == '3')
    {
        if (users.find(name) == users.end())
        {
            int res = mysql_query(con, cmd.c_str());
            std::cout << (res == 0 ? "1\n" : "0\n");
        }
        else
            std::cout << "0\n";
    }
    else if (flag == '2')
    {
        if (users.find(name) != users.end() && users[name] == passwd)
        {
            std::cout << "1\n";
        }
        else
            std::cout << "0\n";
    }
    else
    {
        std::cout << "0\n";
    }
    mysql_free_result(result);

#endif

#ifdef CGISQLPOOL
    std::ifstream file(argv[2]);
    std::string line_str;
    while (getline(file, line_str))
    {
        std::string id, passwd;
        std::stringstream id_passwd(line_str);
        getline(id_passwd, id, ' ');
        getline(id_passwd, passwd, ' ');
        users[id] = passwd;
    }
    std::string name(argv[0]), passwd(argv[1]);
    if (users.find(name) != users.end() && users[name] == passwd)
        std::cout << "1\n";
    else
        std::cout << "0\n";
#endif
    return 0;
}