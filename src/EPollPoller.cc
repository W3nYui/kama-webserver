#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <EPollPoller.h>
#include <Logger.h>
#include <Channel.h>

const int kNew = -1;    // 某个channel还没添加至Poller          // channel的成员index_初始化为-1
const int kAdded = 1;   // 某个channel已经添加至Poller
const int kDeleted = 2; // 某个channel已经从Poller删除

EPollPoller::EPollPoller(EventLoop *loop) // 构造函数函数
    : Poller(loop) // 利用基类Poller的构造函数 来创建EPollPoller对象 以实现IO复用的具体实现的多态
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC)) 
    , events_(kInitEventListSize) // vector<epoll_event>(16) 初始化大小为16，这是一个存放epoll_wait返回的所有发生的事件的文件描述符事件集
{
    if (epollfd_ < 0)
    {
        LOG_FATAL<<"epoll_create error:%d \n"<<errno;
    }
}

EPollPoller::~EPollPoller()
{
    ::close(epollfd_); // 析构函数 由于是系统资源 需要手动销毁 epoll 实例，归还资源
}

// poll函数是EPollPoller的核心函数 负责监听事件并返回发生事件的channel列表
Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    // 由于频繁调用poll 实际上应该用LOG_DEBUG输出日志更为合理 当遇到并发场景 关闭DEBUG日志提升效率
    LOG_INFO<<"fd total count:"<<channels_.size();
    // 这句话的意思是：调用epoll_wait函数 使epollfd_这一实例从events_的开始，复制并监听最多该数组大小的事件，并最多等待timeoutMs毫秒
    // 返回监听得到的事件数量 numEvents
    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int saveErrno = errno; // 由于epoll_wait可能被信号中断而返回-1，此时errno会被设置为EINTR。为了避免在处理错误时误判为其他错误，将errno的值保存到saveErrno变量中，以便后续检查和处理。
    Timestamp now(Timestamp::now()); // 获取当前时间戳 以便于后续处理事件时能够知道事件发生的时间点
    // 如果监听到的时间数量大于0 则有事件发生 需要对channel进行填充
    if (numEvents > 0)
    {
        LOG_INFO<<"events happend"<<numEvents; // LOG_DEBUG最合理
        fillActiveChannels(numEvents, activeChannels);
        if (numEvents == events_.size()) // 扩容操作 避免数组太小 监听不到更多的事件
        {
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents == 0) // 没有事件发生 监听超时 日志返回
    {
        LOG_DEBUG<<"timeout!";
    }
    else // 监听出错 根据返回类型
    {
        if (saveErrno != EINTR) // 说明不是被信号中断 而是其他错误 记录日志
        {
            errno = saveErrno; // 恢复errno的值 以便于后续处理错误时能够正确识别错误类型
            LOG_ERROR<<"EPollPoller::poll() error!";
        }
    }
    return now; // 返回当前时间戳
}

// channel update remove => EventLoop updateChannel removeChannel => Poller updateChannel removeChannel
void EPollPoller::updateChannel(Channel *channel)
{
    const int index = channel->index();
    LOG_INFO<<"func =>"<<"fd"<<channel->fd()<<"events="<<channel->events()<<"index="<<index;

    if (index == kNew || index == kDeleted) 
    {
        if (index == kNew) // 该channel还没有在poller中注册过 即index = -1
        {
            int fd = channel->fd(); // 获取channel的fd
            channels_[fd] = channel; // 将channel添加到channels_中 以便于后续通过fd能够找到channel通道
        }
        else // 该index在channel中已经被注册过 但是已经从该poller中删除了 即index = 2
        { // 不做任何处理 重新添加到poller中
        }
        channel->set_index(kAdded); // 将该channel设置为已注册且添加到poller中 即index = 1
        update(EPOLL_CTL_ADD, channel);
    }
    else // channel已经在Poller中注册过了
    {
        int fd = channel->fd(); // 获取channel的fd 以便于通过fd能够找到channel通道
        if (channel->isNoneEvent()) // 如果该channel不再关注任何事件了 就从poller中删除掉
        {
            update(EPOLL_CTL_DEL, channel); // 从poller中删除掉该channel 即index = 2
            channel->set_index(kDeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel); // 该channel仍然关注事件 但是事件发生了变化 需要修改poller中该channel的事件
        }
    }
}

// 从Poller中删除channel
void EPollPoller::removeChannel(Channel *channel)
{
    int fd = channel->fd();
    channels_.erase(fd);

    LOG_INFO<<"removeChannel fd="<<fd;

    int index = channel->index();
    if (index == kAdded)
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}

// 按照顺序 在activeChannels列表内 填写活跃的channel及其对应发生的事件类型
void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        Channel *channel = static_cast<Channel *>(events_[i].data.ptr); // 获取活跃的channel通道
        channel->set_revents(events_[i].events); // 将当前channel发生的时间保存在channel的revents_中 以便于后续处理事件时能够知道事件发生的类型
        activeChannels->push_back(channel); // EventLoop就拿到了它的Poller给它返回的所有发生事件的channel列表了
    }
}

// 更新channel通道 其实就是调用epoll_ctl add/mod/del
void EPollPoller::update(int operation, Channel *channel)
{
    epoll_event event; // 定义一个epoll_event结构体变量 用于设置epoll_ctl的参数 告知该epoll需要监听的事件类型、目标(文件描述符)、用户自定义数据(channel地址)
    ::memset(&event, 0, sizeof(event));

    int fd = channel->fd();

    event.events = channel->events(); // 将channel中设置的 关注的事件 注册到 epollfd_中
    event.data.fd = fd; // 无效设置 因为 event.data.ptr = channel;在后面 会覆盖event.data.fd = fd
    // fd就被隐藏掉了 取而代之的是channel的指针 不需要再查找哈希表
    event.data.ptr = channel;

    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR<<"epoll_ctl del error:"<<errno;
        }
        else
        {
            LOG_FATAL<<"epoll_ctl add/mod error:"<<errno;
        }
    }
}