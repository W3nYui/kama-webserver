#include <Poller.h>
#include <Channel.h>

Poller::Poller(EventLoop *loop) // 构造时传入Poller所属的事件循环EventLoop 且不会改变
    : ownerLoop_(loop)
{
}

bool Poller::hasChannel(Channel *channel) const
{
    auto it = channels_.find(channel->fd()); // 通过fd找到channel通道类型 以判断参数channel是否在当前的Poller当中
    return it != channels_.end() && it->second == channel; // 双重验证 以防止fd复用导致的误判
}