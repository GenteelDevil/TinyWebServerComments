/*************************************************************
*循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;  
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>

#include <iostream>

#include "../lock/locker.h"
using namespace std;

template <class T>
class block_queue {
   public:
    // 如果不传入参数，初始化一个最大为1000的阻塞队列
    block_queue(int max_size = 1000) {
        if (max_size <= 0) {
            exit(-1);
        }

        m_max_size = max_size;
        // 分配空间（但是尚未使用）
        m_array = new T[max_size];
        // 初始没有使用，队列大小为0
        m_size = 0;
        // 设置阻塞队列头、尾
        // 两个都初始化为-1，说明含义都是：下一个位置指向真正的队头、队尾元素
        m_front = -1;
        m_back = -1;
    }
    // 清除队列
    void clear() {
        // 互斥对于队列的清除：实际上就是修改队头、尾的位置，队列的大小
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }
    // 析构函数，释放队列空间
    ~block_queue() {
        m_mutex.lock();
        // 先对队列指针进行检查，避免释放空指针
        if (m_array != NULL)
            delete[] m_array;

        m_mutex.unlock();
    }
    //判断队列是否满了
    bool full() {
        // 互斥对队列的相关成员进行修改
        m_mutex.lock();
        // 判断队列是否已满
        if (m_size >= m_max_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    //判断队列是否为空
    bool empty() {
        // 互斥修改队列相关数据成员
        m_mutex.lock();
        if (0 == m_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    // 返回队首元素
    // 通过引用返回队首元素，函数实际返回获取状态
    bool front(T &value) {
        // 互斥修改
        m_mutex.lock();
        // 如果队列为空，返回false
        if (0 == m_size) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    // 返回队尾元素
    bool back(T &value) {
        // 互斥修改队列相关数据成员
        m_mutex.lock();
        // 如果队列为空，返回false
        if (0 == m_size) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    // 返回当前的队列大小
    int size() {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }
    // 返回阻塞队列的最大值
    int max_size() {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();
        return tmp;
    }
    //往队列添加元素，需要将所有使用队列的线程先唤醒
    //当有元素push进队列,相当于生产者生产了一个元素
    //若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T &item) {
        m_mutex.lock();
        // 如果当前队列已满，则不能继续push，条件变量广播通知线程进行处理
        if (m_size >= m_max_size) {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }
        // 为了防止溢出，或者说，阻塞队列看起来就是一个循环队列
        // 先增加队尾，然后放入队列元素
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;

        m_size++;
        // 条件变量通知相关线程对队列元素进行处理
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    // pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T &item) {
        m_mutex.lock();
        while (m_size <= 0) {
            if (!m_cond.wait(m_mutex.get())) {
                m_mutex.unlock();
                return false;
            }
        }
        // 是一个队头追赶队尾的过程
        // 明确队头和队尾变量的含义+本质是一个循环队列，就不会弄错
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    // 增加了超时处理，在一定时间内pop失败，就直接返回false
    bool pop(T &item, int ms_timeout) {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (m_size <= 0) {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t)) {
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0) {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

   private:
    locker m_mutex;
    cond m_cond;
    // 阻塞队列指针
    T *m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};

#endif
