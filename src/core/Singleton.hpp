//
// 单例模式基类模板
// 提供线程安全的单例实现
//

#pragma once

namespace Tina::Core {

template<typename T>
class Singleton {
public:
    // 获取单例实例
    static T& getInstance() {
        static T instance;
        return instance;
    }

    // 禁用拷贝
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

    // 禁用移动
    Singleton(Singleton&&) = delete;
    Singleton& operator=(Singleton&&) = delete;

protected:
    // 保护构造函数
    Singleton() = default;
    virtual ~Singleton() = default;
};

} // namespace Tina::Core