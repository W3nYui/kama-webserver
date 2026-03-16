#include <sys/epoll.h>

#include <Channel.h>
#include <EventLoop.h>
#include <Logger.h>

const int Channel::kNoneEvent = 0; //空事件
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI; //读事件
const int Channel::kWriteEvent = EPOLLOUT; //写事件

// EventLoop: ChannelList Poller
Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop) // 从上层传递当前 EventLoop
    , fd_(fd) // Poller需要监听的对象
    , events_(0) // 关注事件初始化
    , revents_(0) // poller返回事件初始化
    , index_(-1) // 当前channel(其代表的fd)在Poller中的状态 -1未添加 1已添加 2已删除
    , tied_(false) // 用于解决 tcp 与 channel 的析构关系
{
}

Channel::~Channel()
{
}

// channel的tie方法什么时候调用过?  TcpConnection => channel
/**
 * TcpConnection中注册了Channel对应的回调函数，传入的回调函数均为TcpConnection
 * 对象的成员方法，因此可以说明一点就是：Channel的结束一定晚于TcpConnection对象！
 * 此处用tie去解决TcpConnection和Channel的生命周期时长问题，从而保证了Channel对象能够在
 * TcpConnection销毁前销毁。
 **/
void Channel::tie(const std::shared_ptr<void> &obj)
{
    tie_ = obj; // 获取上层对象 即实例化channel的对象的智能指针
    tied_ = true;
}
//update 和remove => EpollPoller 更新channel在poller中的状态
/**
 * 当改变channel所表示的fd的events事件后，update负责再poller里面更改fd相应的事件epoll_ctl
 **/
void Channel::update() // 向上层调用 因为channel与poll解耦
{
    // 通过channel所属的eventloop，调用poller的相应方法，注册fd的events事件
    loop_->updateChannel(this); // 从loop -> cahnnel -> loop-> poller 的注册方式 构造后又回传
}

// 在channel所属的EventLoop中把当前的channel删除掉
void Channel::remove() // 向上层调用 因为channel与poll解耦
{
    loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_)
    {
        std::shared_ptr<void> guard = tie_.lock(); // 检查上层对象是否还存在 利用weak指针检查
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }
        // 如果提升失败了 就不做任何处理 说明Channel的TcpConnection对象已经不存在了
    }
    else // 当组件生命周期安全可控时 不需要使用tie机制
    {
        handleEventWithGuard(receiveTime);
    }
}

void Channel::handleEventWithGuard(Timestamp receiveTime) // 用来匹配channel的事件类型 调用相应的回调函数
{
    LOG_INFO<<"channel handleEvent revents:"<<revents_;
    // 关闭
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) { // 返回挂起事件，且没有读事件 说明对端关闭
        if (closeCallback_) {
            closeCallback_();
        }
    }
    // 错误
    if (revents_ & EPOLLERR) {
        if (errorCallback_) {
            errorCallback_();
        }
    }
    // 读
    if (revents_ & (EPOLLIN | EPOLLPRI)) {
        if (readCallback_) {
            readCallback_(receiveTime);
        }
    }
    // 写
    if (revents_ & EPOLLOUT) {
        if (writeCallback_) {
            writeCallback_();
        }
    }
}