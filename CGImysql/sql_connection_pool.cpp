#include "sql_connection_pool.h"

#include <mysql/mysql.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <list>
#include <string>

using namespace std;

connection_pool::connection_pool() {
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance() {
    // 使用单例模式
    static connection_pool connPool;
    return &connPool;
}

// 构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log) {
    // 通过初始化传递参数进入
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DBName;
    m_close_log = close_log;

    for (int i = 0; i < MaxConn; i++) {
        // 定义数据库连接指针
        MYSQL *con = NULL;
        con = mysql_init(con);

        if (con == NULL) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        // 连接数据库
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

        if (con == NULL) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        // 将申请成功的数据库连接加入到连接池中
        connList.push_back(con);
        // 增加可用连接数量
        ++m_FreeConn;
    }
    // 信号量设置为申请成功的连接总数
    reserve = sem(m_FreeConn);
    // 数据库最大连接数量设置为申请成功的连接总数 前面传入的MaxConn不一定全部申请成功
    m_MaxConn = m_FreeConn;
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection() {
    MYSQL *con = NULL;

    if (0 == connList.size())
        return NULL;

    // 等到信号可用 有空闲的连接
    reserve.wait();
    // 互斥对连接池的属性进行修改
    lock.lock();
    // 从连接池队列头获取可用连接
    con = connList.front();
    // 将队列头pop掉 那么阻塞队列实际上也可以用STL的list来实现呢
    connList.pop_front();
    // 可用数量-1
    --m_FreeConn;
    // 已经使用+1
    ++m_CurConn;

    lock.unlock();
    return con;
}

// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con) {
    if (NULL == con)
        return false;
    // 互斥对连接池进行修改
    lock.lock();
    // 将使用完的数据库连接压入到连接池中
    connList.push_back(con);
    // 可用连接+1
    ++m_FreeConn;
    // 正在使用-1
    --m_CurConn;

    lock.unlock();
    // 信号量+1
    reserve.post();
    return true;
}

// 销毁数据库连接池
void connection_pool::DestroyPool() {
    // 互斥修改连接池
    lock.lock();
    if (connList.size() > 0) {
        // 定义迭代器
        list<MYSQL *>::iterator it;
        // 将连接池中所有的连接释放掉
        for (it = connList.begin(); it != connList.end(); ++it) {
            MYSQL *con = *it;
            mysql_close(con);
        }
        //
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }

    lock.unlock();
}

// 当前空闲的连接数
int connection_pool::GetFreeConn() {
    return this->m_FreeConn;
}

connection_pool::~connection_pool() {
    DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) {
    *SQL = connPool->GetConnection();

    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII);
}