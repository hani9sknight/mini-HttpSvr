# mini-HttpSvr 
---
## 简介
> * 一个简单、轻量的web服务器DEMO 
> * 使用线程池 + epoll(ET和LT均实现) + 模拟Proactor模式的并发模型
> * 使用状态机解析HTTP请求，实现了解析GET和POST请求
> * 访问服务器数据库实现web端用户注册、登录功能，可以请求服务器图片和视频文件
> * 实现同步/异步日志系统，记录服务器运行状态
> * 经Webbench压力测试可以实现上万的并发连接数据交换

## 如何使用

### 测试环境需求
> * Mysql 5.7以上
> * Linux环境：建议使用Ubuntu或Debain,RedHat系列暂未测试
> * 测试用浏览器：建议使用Chrome；

### 创建数据库

```Sql
    create database <要创建的数据库名> set utf8;

    USE <刚刚创建数据库>;

    CREATE TABLE user(
        username char(50) NULL,
        passwd char(50) NULL
    )ENGINE=InnoDB;

    INSERT INTO user(username, passwd) VALUES('name', 'passwd');
```

### 配置文件
1. 根据您自己的情况修改config.inc中的数据库配置，并选择校验方法、日志写入模式、EPOLL模式。
2. 修改`http/root_path.inc`中的ROOT_PATH宏为root文件夹的绝对路径。

### 生成
        make server

### 运行及测试
* 运行./server <端口号>即可启动,端口号选择未使用的闲置端口。
* 浏览器打开localhost:<端口号>或<本机IP>：<端口号>

## 测试结果

使用ＬT连接情况下，ET和LT连接相比差不多，只是ＱPS稍差：

![压力测试结果]{./root/webbench.png}








