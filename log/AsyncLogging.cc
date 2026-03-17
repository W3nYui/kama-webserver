#include "AsyncLogging.h"
#include <stdio.h>
// 构造函数 初始化成员变量
AsyncLogging::AsyncLogging(const std::string &basename, off_t rollSize, int flushInterval)
    :
      flushInterval_(flushInterval),
      running_(false),
      basename_(basename), // 日志文件基本名称
      rollSize_(rollSize), // 日志文件大小达到多少字节时滚动，单位:字节
      thread_(std::bind(&AsyncLogging::threadFunc, this), "Logging"), // 创建一个线程对象，绑定线程函数threadFunc，并命名为"Logging"
      mutex_(),
      cond_(),
      currentBuffer_(new LargeBuffer), // 创建一个新的LargeBuffer对象，并将其指针赋值给currentBuffer_
      nextBuffer_(new LargeBuffer), // 创建一个新的LargeBuffer对象，并将其指针赋值给nextBuffer_
      buffers_() // 构造一个缓冲区
{
    currentBuffer_->bzero();
    nextBuffer_->bzero();
    buffers_.reserve(16); // 只维持队列长度2~16.
}
// 调用此函数解决前端把LOG_XXX<<"..."传递给后端，后端再将日志消息写入日志文件
void AsyncLogging::append(const char *logline, int len)
{
    std::lock_guard<std::mutex> lg(mutex_);
    // 缓冲区剩余的空间足够写入
    if (currentBuffer_->avail() > static_cast<size_t>(len))
    {
        currentBuffer_->append(logline, len);
    }
    else // 如果缓存空间不足 就要将目前的缓冲区写入buffer 让后端写入io 同时写入新的内容
    { // 这种情况只发生在 线程函数没有被唤醒 就需要交换 且强制唤醒
        buffers_.push_back(std::move(currentBuffer_)); // 将指针移入缓冲区队列buffers_中

        if (nextBuffer_) // 如果nextBuffer_不为空 就将其指针移入currentBuffer_中
        {
            currentBuffer_ = std::move(nextBuffer_); // 移动语义移动指针
        }
        else // 创建一个新的LargeBuffer对象，并将其指针赋值给currentBuffer_
        {
            currentBuffer_.reset(new LargeBuffer);
        }
        currentBuffer_->append(logline, len);
        // 唤醒后端线程写入磁盘
        cond_.notify_one(); // 由于线程休眠 需要唤醒
    }
}

/**
 * @brief 线程函数，负责将日志数据写入磁盘
 * 
 */
void AsyncLogging::threadFunc()
{
    // output写入磁盘接口
    LogFile output(basename_, rollSize_); // 在需要使用的时候才创建LogFile对象，避免不必要的资源占用
    BufferPtr newbuffer1(new LargeBuffer); // 生成新buffer替换currentbuffer_
    BufferPtr newbuffer2(new LargeBuffer); // 生成新buffer2替换newBuffer_，其目的是为了防止后端缓冲区全满前端无法写入
    newbuffer1->bzero();
    newbuffer2->bzero();
    // 缓冲区数组置为16个，用于和前端缓冲区数组进行交换
    BufferVector buffersToWrite;
    buffersToWrite.reserve(16);
    while (running_)
    {
        {
            // 互斥锁保护这样就保证了其他前端线程无法向前端buffer写入数据
            std::unique_lock<std::mutex> lg(mutex_);
            if (buffers_.empty()) // 传输结束 进入flushInterval_时间等待的休眠 直到结束或者被前端线程唤醒
            {
                // 在这句话 unique_lock互斥锁 lg 是临时释放的 便于外部append() 因为在等待
                cond_.wait_for(lg, std::chrono::seconds(flushInterval_)); // 等待flushInterval_时间或者被前端线程唤醒
            }
            buffers_.push_back(std::move(currentBuffer_)); // 将当前缓冲区移动到buffers_中 让后端线程写入磁盘
            currentBuffer_ = std::move(newbuffer1); // 交换一段空的缓冲区给前端写入
            if (!nextBuffer_)
            {
                nextBuffer_ = std::move(newbuffer2);
            }
            buffersToWrite.swap(buffers_); // 交换两段大缓冲区 让前端可以继续写入
        }
        // 前端的数据交换在这里结束了 锁停止 这里开始调用后端
        // 从待写缓冲区取出数据通过LogFile提供的接口写入到磁盘中
        for (auto &buffer : buffersToWrite)
        {
            output.append(buffer->data(), buffer->length()); // 将缓冲区中的数据写入磁盘
        }
        // 用 resize 主动回收内存，剩余两个便于后续交换复用
        if (buffersToWrite.size() > 2) 
        {
            buffersToWrite.resize(2);
        }
        // 当前面的缓冲区被前端交换走了 需要重新生成新的缓冲区来替换它们 以便下一次的写入
        if (!newbuffer1)
        {
            newbuffer1 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newbuffer1->reset(); // 重置缓冲区 以便下次使用
        }
        if (!newbuffer2)
        {
            newbuffer2 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newbuffer2->reset();
        }
        buffersToWrite.clear(); // 清空后端缓冲队列
        output.flush();         // 清空文件夹缓冲区
    }
    output.flush(); // 确保一定清空。
}