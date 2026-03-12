#pragma once

#include <vector>
#include <unordered_map>

#include "noncopyable.h"
#include "Timestamp.h"

class Channel;
class EventLoop;
// 抽象基类 Poller 定义了IO复用的接口 以及一些通用的成员变量和方法
// muduo库中多路事件分发器的核心IO复用模块
class Poller
{
public:
    using ChannelList = std::vector<Channel *>;

    Poller(EventLoop *loop); // 构造函数 传入Poller所属的事件循环EventLoop
    virtual ~Poller() = default; // 虚析构函数 以保证通过基类指针删除派生类对象时能够正确调用派生类的析构函数

    // 给所有IO复用保留统一的接口 都是纯虚函数 由派生类实现
    virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;
    virtual void updateChannel(Channel *channel) = 0; 
    virtual void removeChannel(Channel *channel) = 0;

    // 判断参数channel是否在当前的Poller当中
    bool hasChannel(Channel *channel) const;

    // EventLoop可以通过该接口获取默认的IO复用的具体实现
    static Poller *newDefaultPoller(EventLoop *loop); // 利用基类得到的构造函数 来创建EPollPoller对象或者PollPoller对象 以实现IO复用的具体实现的多态

protected:  
    // map的key:sockfd value:sockfd所属的channel通道类型
    using ChannelMap = std::unordered_map<int, Channel *>; 
    ChannelMap channels_; // O(1)查找某个sockfd所属的channel通道类型 但只属于当前EventLoop 派生类可以继承与访问

private:
    EventLoop *ownerLoop_; // 定义Poller所属的事件循环EventLoop
};