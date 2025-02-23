//
// Created by wuxianggujun on 2025/2/22.
//

#pragma once

#include "tina/core/Core.hpp"
#include <cassert>
#include <stdexcept>
#include <type_traits>
#include <list>

namespace Tina
{
    template <typename T>
    class Delegate;

    template <typename RetVal, typename... Args>
    class Delegate<RetVal(Args...)>
    {
    private:
        using StubPtr = RetVal(*)(void*, Args...);

        template<RetVal(*Function)(Args...)>
        static RetVal functionStub(void*, Args... args)
        {
            return Function(std::forward<Args>(args)...);
        }

        template<typename C, RetVal(C::*Function)(Args...)>
        static RetVal methodStub(void* instance, Args... args)
        {
            C* p = static_cast<C*>(instance);
            return (p->*Function)(std::forward<Args>(args)...);
        }

        template<typename C, RetVal(C::*Function)(Args...) const>
        static RetVal constMethodStub(void* instance, Args... args)
        {
            const C* p = static_cast<const C*>(instance);
            return (p->*Function)(std::forward<Args>(args)...);
        }

        template<typename F>
        static RetVal functorStub(void* instance, Args... args)
        {
            F* f = static_cast<F*>(instance);
            return (*f)(std::forward<Args>(args)...);
        }

    public:
        Delegate() = default;
        Delegate(const Delegate&) = default;
        Delegate& operator=(const Delegate&) = default;
        Delegate(Delegate&&) noexcept = default;
        Delegate& operator=(Delegate&&) noexcept = default;
        ~Delegate() = default;

        // 绑定静态函数
        template <RetVal(*Function)(Args...)>
        void bind()
        {
            m_instance = nullptr;
            m_stub = &functionStub<Function>;
            m_storage.reset();
        }

        // 绑定非const成员函数
        template <typename C, RetVal(C::*Function)(Args...)>
        void bind(C* instance)
        {
            m_instance = instance;
            m_stub = &methodStub<C, Function>;
            m_storage.reset();
        }

        // 绑定const成员函数
        template <typename C, RetVal(C::*Function)(Args...) const>
        void bind(const C* instance)
        {
            m_instance = const_cast<C*>(instance);
            m_stub = &constMethodStub<C, Function>;
            m_storage.reset();
        }

        // 绑定函数对象（支持lambda等）
        template <typename F, typename = std::enable_if_t<std::is_invocable_r_v<RetVal, F, Args...>>>
        void bind(F&& functor)
        {
            m_storage = std::make_unique<Storage<F>>(std::forward<F>(functor));
            m_instance = m_storage->ptr();
            m_stub = &functorStub<F>;
        }

        // 检查是否绑定
        [[nodiscard]] bool isEmpty() const { return m_stub == nullptr; }

        // 调用委托
        RetVal invoke(Args... args) const
        {
            if (!m_stub)
            {
                if constexpr (std::is_same_v<RetVal, void>)
                {
                    return;
                }
                else
                {
                    throw std::runtime_error("Cannot invoke unbound Delegate");
                }
            }
            return m_stub(m_instance, std::forward<Args>(args)...);
        }

        // 清除绑定
        void clear()
        {
            m_instance = nullptr;
            m_stub = nullptr;
            m_storage.reset();
        }

    private:
        // 存储函数对象的基类
        struct StorageBase
        {
            virtual ~StorageBase() = default;
            virtual void* ptr() = 0;
        };

        // 具体函数对象存储
        template <typename Func>
        struct Storage final : public StorageBase
        {
            Func functor;
            explicit Storage(Func&& func) : functor(std::forward<Func>(func)) {}
            void* ptr() override { return &functor; }
        };

        void* m_instance = nullptr;
        StubPtr m_stub = nullptr;
        UniquePtr<StorageBase> m_storage;
    };

    template <typename T>
    class MulticastDelegate;

    template <typename RetVal, typename... Args>
    class MulticastDelegate<RetVal(Args...)>
    {
    public:
        MulticastDelegate() = default;
        MulticastDelegate(const MulticastDelegate&) = delete;
        MulticastDelegate& operator=(const MulticastDelegate&) = delete;
        MulticastDelegate(MulticastDelegate&&) = default;
        MulticastDelegate& operator=(MulticastDelegate&&) = default;
        ~MulticastDelegate() = default;

        // 绑定静态函数
        template <RetVal(*Function)(Args...)>
        void bind()
        {
            Delegate<RetVal(Args...)> delegate;
            delegate.template bind<Function>();
            m_delegates.emplace_back(std::move(delegate));
        }

        // 绑定非const成员函数
        template <typename C, RetVal(C::*Function)(Args...)>
        void bind(C* instance)
        {
            Delegate<RetVal(Args...)> delegate;
            delegate.template bind<C, Function>(instance);
            m_delegates.emplace_back(std::move(delegate));
        }

        // 绑定const成员函数
        template <typename C, RetVal(C::*Function)(Args...) const>
        void bind(const C* instance)
        {
            Delegate<RetVal(Args...)> delegate;
            delegate.template bind<C, Function>(instance);
            m_delegates.emplace_back(std::move(delegate));
        }

        // 绑定函数对象（支持lambda等）
        template <typename F, typename = std::enable_if_t<std::is_invocable_r_v<RetVal, F, Args...>>>
        void bind(F&& functor)
        {
            Delegate<RetVal(Args...)> delegate;
            delegate.bind(std::forward<F>(functor));
            m_delegates.emplace_back(std::move(delegate));
        }

        // 检查是否为空
        [[nodiscard]] bool isEmpty() const { return m_delegates.empty(); }

        // 调用所有委托
        void invoke(Args... args) const
        {
            for (const auto& delegate : m_delegates)
            {
                delegate.invoke(std::forward<Args>(args)...);
            }
        }

        // 清除所有绑定
        void clear()
        {
            m_delegates.clear();
        }

    private:
        std::list<Delegate<RetVal(Args...)>> m_delegates;
    };
} // namespace Tina
