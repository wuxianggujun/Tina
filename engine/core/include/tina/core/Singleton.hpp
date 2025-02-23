#pragma once

namespace Tina {

/**
 * @brief 单例模板类
 * 
 * 使用此模板类创建线程安全的单例类。
 * 示例：typedef Singleton<MyClass> MySingleton;
 * 
 * @tparam T 需要实现单例的类型
 */
template <typename T>
class Singleton {
public:
    /**
     * @brief 获取单例实例
     * 
     * @return T* 单例实例的指针
     */
    static T* getInstance() {
        static T instance;
        return &instance;
    }

protected:
    Singleton() = default;
   virtual ~Singleton() = default;

private:
    // 禁用拷贝构造和赋值操作
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    
    // C++11 禁用移动构造和赋值
    Singleton(Singleton&&) = delete;
    Singleton& operator=(Singleton&&) = delete;
};

} // namespace Tina 