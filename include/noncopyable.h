#pragma once // 防止头文件重复包含

/**
 * noncopyable被继承后 派生类对象可正常构造和析构 但派生类对象无法进行拷贝构造和赋值构造
 **/
class noncopyable // 作为基类存在
{
public:
    // = delete C++11语法 不要为我生成这个函数，如果有人尝试调用它，请直接报错
    noncopyable(const noncopyable &) = delete; // 删除 拷贝构造函数
    noncopyable &operator=(const noncopyable &) = delete; // 删除 赋值构造函数
    // void operator=(const noncopyable &) = delete;    // muduo将返回值变为void 这其实无可厚非
protected:
    noncopyable() = default; // 允许构造和析构
    ~noncopyable() = default;
};