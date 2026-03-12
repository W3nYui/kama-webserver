#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"
#include "TimerQueue.h"
class Channel;
class Poller;

// 事件循环类 主要包含了两个大模块 Channel Poller(epoll的抽象)
class EventLoop : noncopyable
{
public:
    using Functor = std::function<void()>;
    // 构造函数与析构函数 没有拷贝与复制构造 因为继承了noncopyable类 禁止了拷贝与复制构造
    // 不需要指针传入 因为EventLoop对象在创建时就会创建一个属于自己的Poller对象和TimerQueue对象
    EventLoop();
    ~EventLoop();

    // 开启事件循环
    void loop();
    // 退出事件循环
    void quit();

    // 返回Poller检测到发生事件的Channels的时间点
    Timestamp pollReturnTime() const { return pollRetureTime_; }

    // 在当前loop中执行
    void runInLoop(Functor cb);
    // 把上层注册的回调函数cb放入队列中 唤醒loop所在的线程执行cb
    void queueInLoop(Functor cb);

    // 通过eventfd唤醒loop所在的线程
    void wakeup();

    // EventLoop的方法 => Poller的方法
    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);

    // 判断EventLoop对象是否在自己的线程里
    // 调用CurrentThread库的tid() 其tid()是一个内连 函数 通过__builtin_expect优化 且缓存有ID
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); } // threadId_为EventLoop创建时的线程id CurrentThread::tid()为当前线程id
    /**
     * 定时任务相关函数
     */
    void runAt(Timestamp timestamp, Functor &&cb) // 在timestamp时间点执行cb回调函数
    {
        timerQueue_->addTimer(std::move(cb), timestamp, 0.0); // 在TimerQueue中添加一个定时器 该定时器的回调函数为cb 到期时间为timestamp 不重复执行
    }
    /**
     * @brief waitTime秒后执行cb回调函数 
     * 
     * @param waitTime 
     * @param cb 
     */
    void runAfter(double waitTime, Functor &&cb)    
    {
        // 利用Timestamp库函数 获取当前时间戳 加上延时的秒数 得到新增时后的时间戳
        Timestamp time(addTime(Timestamp::now(), waitTime));
        runAt(time, std::move(cb));
    }
    /**
     * @brief 间隔interval秒重复执行cb回调函数。
     * 在interval秒后第一次执行cb回调函数 后续每隔interval秒执行一次cb回调函数
     * @param interval 
     * @param cb 
     */
    void runEvery(double interval, Functor &&cb)
    {
        Timestamp timestamp(addTime(Timestamp::now(), interval));
        timerQueue_->addTimer(std::move(cb), timestamp, interval);
    }

private:
    void handleRead();        // 给eventfd返回的文件描述符wakeupFd_绑定的事件回调 当wakeup()时 即有事件发生时 调用handleRead()读wakeupFd_的8字节 同时唤醒阻塞的epoll_wait
    void doPendingFunctors(); // 执行上层回调

    using ChannelList = std::vector<Channel *>;

    std::atomic_bool looping_; // 原子操作 底层通过CAS实现
    std::atomic_bool quit_;    // 标识退出loop循环

    const pid_t threadId_; // 记录当前EventLoop是被哪个线程id创建的 即标识了当前EventLoop的所属线程id

    Timestamp pollRetureTime_; // Poller返回发生事件的Channels的时间点
    std::unique_ptr<Poller> poller_;
    std::unique_ptr<TimerQueue> timerQueue_;
    int wakeupFd_; // 作用：当mainLoop获取一个新用户的Channel 需通过轮询算法选择一个subLoop 通过该成员唤醒subLoop处理Channel
    std::unique_ptr<Channel> wakeupChannel_;
    // 构建一个事件列表 用于返回Poller检测到发生事件的Channels
    ChannelList activeChannels_;

    std::atomic_bool callingPendingFunctors_; // 标识当前loop是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_;    // 存储loop需要执行的所有回调操作
    std::mutex mutex_;                        // 互斥锁 用来保护上面vector容器的线程安全操作
};