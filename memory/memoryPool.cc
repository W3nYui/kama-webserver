#include "memoryPool.h"

namespace memoryPool 
{
MemoryPool::MemoryPool(size_t BlockSize)
    : BlockSize_ (BlockSize)
{}

MemoryPool::~MemoryPool()
{
    // 把连续的block删除
    Slot* cur = firstBlock_;
    while (cur)
    {
        Slot* next = cur->next;
        // 等同于 free(reinterpret_cast<void*>(firstBlock_));
        // 转化为 void 指针，因为 void 类型不需要调用析构函数，只释放空间
        operator delete(reinterpret_cast<void*>(cur));
        cur = next;
    }
}

// 内存池的大小默认是4KB，只是基于每个内存槽的大小来区分为不同的内存池
void MemoryPool::init(size_t size) // 初始化内存池
{ // 内存池初始化得到内存的槽大小 以及内存块内部的指针
    assert(size > 0);
    SlotSize_ = size; // 内存池槽大小
    firstBlock_ = nullptr;
    curSlot_ = nullptr;
    freeList_ = nullptr;
    lastSlot_ = nullptr;
}

void* MemoryPool::allocate() // 获取内存
{
    // 优先使用空闲链表中的内存槽
    if (freeList_ != nullptr) // 内存池利用freeList指向的空闲槽分配内存 如果有可以直接分配
    {
        {
            std::lock_guard<std::mutex> lock(mutexForFreeList_);
            if (freeList_ != nullptr)
            {
                Slot* temp = freeList_;
                freeList_ = freeList_->next; // 更新空闲链表头指针
                return temp;
            }
        }
    }

    Slot* temp; // 因为该内存槽已经被占用了 所以需要一个临时指针来保存当前内存槽的位置
    {   
        std::lock_guard<std::mutex> lock(mutexForBlock_);
        if (curSlot_ >= lastSlot_) // 没有可用的内存槽了 需要申请新的内存块
        {
            // 当前内存块已无内存槽可用，开辟一块新的内存 接在这块内存块的头部
            allocateNewBlock();
        }
    
        temp = curSlot_;
        // 这里不能直接 curSlot_ += SlotSize_ 因为curSlot_是Slot*类型，所以需要除以SlotSize_再加1
        curSlot_ += SlotSize_ / sizeof(Slot);
    }
    
    return temp; // 返回可用的内存槽地址
}

void MemoryPool::deallocate(void* ptr) // 回收内存
{
    if (ptr)
    {
        // 回收内存，将内存通过头插法插入到空闲链表的头部
        std::lock_guard<std::mutex> lock(mutexForFreeList_);
        reinterpret_cast<Slot*>(ptr)->next = freeList_;
        freeList_ = reinterpret_cast<Slot*>(ptr);
    }
}

void MemoryPool::allocateNewBlock()
{   
    //std::cout << "申请一块内存块，SlotSize: " << SlotSize_ << std::endl;
    // 头插法插入新的内存块
    void* newBlock = operator new(BlockSize_); // 新申请一个4KB的内存块
    reinterpret_cast<Slot*>(newBlock)->next = firstBlock_; // 将新内存与旧内存块连接起来
    firstBlock_ = reinterpret_cast<Slot*>(newBlock); // 更新首地址指针

    char* body = reinterpret_cast<char*>(newBlock) + sizeof(Slot*); // 内存块的前面是一个指针域 用来连接内存块链表 所以实际可用内存要从sizeof(Slot*)之后开始
    size_t paddingSize = padPointer(body, SlotSize_); // 计算对齐需要填充内存的大小
    curSlot_ = reinterpret_cast<Slot*>(body + paddingSize); // 将前面的内存块进行对齐 后移得到新的内存块的第一个内存槽的位置

    // 超过该标记位置，则说明该内存块已无内存槽可用，需向系统申请新的内存块
    lastSlot_ = reinterpret_cast<Slot*>(reinterpret_cast<size_t>(newBlock) + BlockSize_ - SlotSize_ + 1);

    freeList_ = nullptr;
}

// 让指针对齐到槽大小的倍数位置
size_t MemoryPool::padPointer(char* p, size_t align)
{
    // align 是槽大小
    return (align - reinterpret_cast<size_t>(p)) % align;
}

void HashBucket::initMemoryPool()
{
    for (int i = 0; i < MEMORY_POOL_NUM; i++) // 获取64个内存池 并定义每个内存池的槽大小为8的倍数
    {
        getMemoryPool(i).init((i + 1) * SLOT_BASE_SIZE); // 多级哈希桶
    }
}   

// 单例模式
MemoryPool& HashBucket::getMemoryPool(int index)
{
    static MemoryPool memoryPool[MEMORY_POOL_NUM]; // 静态局部变量，第一次调用时初始化，之后直接返回 返回的是该内存池的引用
    return memoryPool[index];
}

} // namespace memoryPool
