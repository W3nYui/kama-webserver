#pragma once 

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>

namespace memoryPool
{
#define MEMORY_POOL_NUM 64 // 内存池数目
#define SLOT_BASE_SIZE 8    // 内存池槽大小的基数，槽大小为该基数的倍数
#define MAX_SLOT_SIZE 512 // 内存池槽大小的上限，超过该上限的内存分配将直接使用new


/* 具体内存池的槽大小没法确定，因为每个内存池的槽大小不同(8的倍数)
   所以这个槽结构体的sizeof 不是实际的槽大小 */
struct Slot 
{
    Slot* next;
};

class MemoryPool
{
public:
    MemoryPool(size_t BlockSize = 4096); // 默认内存块大小为4KB
    ~MemoryPool();
    
    void init(size_t);

    void* allocate();
    void deallocate(void*);
private:
    void allocateNewBlock();
    size_t padPointer(char* p, size_t align);

private:
    int        BlockSize_; // 内存块大小
    int        SlotSize_; // 槽大小
    Slot*      firstBlock_; // 指向内存池管理的首个实际内存块
    Slot*      curSlot_; // 指向当前未被使用过的槽
    Slot*      freeList_; // 指向空闲的槽(被使用过后又被释放的槽)
    Slot*      lastSlot_; // 作为当前内存块中最后能够存放元素的位置标识(超过该位置需申请新的内存块)
    std::mutex mutexForFreeList_; // 保证freeList_在多线程中操作的原子性
    std::mutex mutexForBlock_; // 保证多线程情况下避免不必要的重复开辟内存导致的浪费行为
};


class HashBucket // 利用哈希桶管理多个内存池 并提供接口
{
public:
    static void initMemoryPool();
    static MemoryPool& getMemoryPool(int index);

    static void* useMemory(size_t size) // 根据元素大小选取合适的内存池分配内存
    {
        if (size <= 0)
            return nullptr;
        if (size > MAX_SLOT_SIZE) // 大于512字节的内存，则使用new
            return operator new(size); // 超过了预分配的内存池管理范围，直接使用new分配内存

        // 相当于size / 8 向上取整（因为分配内存只能大不能小
        return getMemoryPool(((size + 7) / SLOT_BASE_SIZE) - 1).allocate(); // 调用对应内存池的allocate方法分配内存
    }

    static void freeMemory(void* ptr, size_t size) // 释放内存
    {
        if (!ptr)
            return;
        if (size > MAX_SLOT_SIZE)
        {
            operator delete(ptr);
            return;
        }

        getMemoryPool(((size + 7) / SLOT_BASE_SIZE) - 1).deallocate(ptr); // 调用对应内存池的deallocate方法释放内存
    }

    template<typename T, typename... Args> 
    friend T* newElement(Args&&... args);
    
    template<typename T>
    friend void deleteElement(T* p);
};

// 上面都是工具类的实现 下面是对外开放的接口函数模板
template<typename T, typename... Args> // 可变参数模板，允许传入任意数量和类型的参数
T* newElement(Args&&... args) // 类型的完美转发，保持参数的左值或右值属性
{
    T* p = nullptr;
    // 根据元素大小选取合适的内存池分配内存
    if ((p = reinterpret_cast<T*>(HashBucket::useMemory(sizeof(T)))) != nullptr) // 使用内存池获取内存 并重新定义指针类型
        // 在分配的内存上构造对象
        new(p) T(std::forward<Args>(args)...);

    return p;
}

template<typename T>
void deleteElement(T* p)
{
    // 对象析构
    if (p)
    {
        p->~T();
         // 内存回收
        HashBucket::freeMemory(reinterpret_cast<void*>(p), sizeof(T));
    }
}

} // namespace memoryPool