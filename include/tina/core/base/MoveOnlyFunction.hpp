#pragma once

#include <tina/core/base/Types.hpp>

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace Tina::Core {

// Owning, move-only, type-erased callable.
//
// Replaces std::move_only_function, which libc++ still has not implemented: NDK 28
// ships libc++ 19 and NDK 29 ships libc++ 21, and both leave
// `__cpp_lib_move_only_function` commented out in <version> while enabling their other
// C++23 library features. That is a library gap, not a language-level one -- Clang
// accepts -std=c++23 fine -- so waiting for a newer NDK does not fix it, and Android
// cannot compile a single Tina public header without this type.
//
// Deliberately not std::function: that requires the target to be copyable, and every
// user here owns a unique_ptr or another move-only capture. It also allocates silently,
// which this codebase forbids for bounded storage.
//
// Small targets live in a fixed inline buffer; larger ones are heap-allocated, exactly
// as std::move_only_function does.
//
// I first wrote this inline-only, on the reasoning that a silent allocation would violate
// the bounded-storage invariant. That is wrong here, and the compiler proved it:
// TaskGroup::add wraps a caller's TaskCallable in another TaskCallable to attach
// completion bookkeeping, so the outer target captures a whole inner MoveOnlyFunction.
// Since sizeof(MoveOnlyFunction) is always capacity + a vtable pointer, such a target is
// *by construction* larger than the buffer it must fit into -- no capacity value can ever
// satisfy it. Raising the size from 128 to 256 just moved the same failure.
//
// The invariant this codebase actually holds is that *bounded queues and arenas* do not
// silently grow: a full queue returns CapacityExceeded rather than allocating. A
// type-erased callable is not one of those; it is a value whose size depends on its
// target, and wrapping a work item is a legitimate pattern rather than a budget overrun.
// So the inline buffer stays as an allocation-avoidance optimisation for the common small
// captures, and oversized targets allocate once at construction.
inline constexpr usize MoveOnlyFunctionInlineCapacity = 128;
inline constexpr usize MoveOnlyFunctionInlineAlignment = alignof(std::max_align_t);

template <typename Signature>
class MoveOnlyFunction;

template <typename Result, typename... Args>
class MoveOnlyFunction<Result(Args...)> final {
  public:
    MoveOnlyFunction() noexcept = default;
    MoveOnlyFunction(std::nullptr_t) noexcept {}

    template <typename Callable,
              typename Decayed = std::decay_t<Callable>,
              typename = std::enable_if_t<
                  !std::is_same_v<Decayed, MoveOnlyFunction> &&
                  std::is_invocable_r_v<Result, Decayed&, Args...>>>
    MoveOnlyFunction(Callable&& callable)
    {
        static_assert(std::is_nothrow_move_constructible_v<Decayed>,
                      "Tina::Core::MoveOnlyFunction target must be nothrow move constructible so "
                      "moving a queued work item cannot throw");
        if constexpr (fitsInline<Decayed>())
        {
            ::new (static_cast<void*>(m_storage)) Decayed(std::forward<Callable>(callable));
            m_operations = &inlineOperationsFor<Decayed>;
        }
        else
        {
            // One allocation at construction. Moving afterwards only moves the pointer,
            // so a queued work item still moves without allocating or throwing.
            Decayed* const target = new Decayed(std::forward<Callable>(callable));
            ::new (static_cast<void*>(m_storage)) HeapTargetPointer(target);
            m_operations = &heapOperationsFor<Decayed>;
        }
    }

    ~MoveOnlyFunction()
    {
        destroyTarget();
    }

    MoveOnlyFunction(const MoveOnlyFunction&) = delete;
    MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;

    MoveOnlyFunction(MoveOnlyFunction&& other) noexcept
    {
        adoptFrom(other);
    }

    MoveOnlyFunction& operator=(MoveOnlyFunction&& other) noexcept
    {
        if (this != &other)
        {
            destroyTarget();
            adoptFrom(other);
        }
        return *this;
    }

    MoveOnlyFunction& operator=(std::nullptr_t) noexcept
    {
        destroyTarget();
        return *this;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_operations != nullptr;
    }

    // Non-const like std::move_only_function's: a target is allowed to mutate its own
    // captures, so callers hold these by value or by non-const reference.
    Result operator()(Args... args)
    {
        return m_operations->invoke(m_storage, std::forward<Args>(args)...);
    }

  private:
    // A heap target is reached through a pointer parked in the same inline buffer, so
    // both storage strategies share one set of hooks and one move path.
    using HeapTargetPointer = void*;

    // `relocate` moves the target to raw destination storage *and* ends the source's
    // lifetime, rather than leaving a husk for the caller to destroy afterwards. The two
    // strategies disagree on what that costs, and the split has to live here: the inline
    // husk still needs its destructor run, while the heap pointer must specifically not be
    // deleted -- the destination now owns that allocation. A caller-side destroy after the
    // move would be right for one and a double free for the other.
    struct Operations final {
        Result (*invoke)(void* storage, Args&&... args);
        void (*relocate)(void* sourceStorage, void* destinationStorage) noexcept;
        void (*destroy)(void* storage) noexcept;
    };

    template <typename Decayed>
    [[nodiscard]] static constexpr bool fitsInline() noexcept
    {
        return sizeof(Decayed) <= MoveOnlyFunctionInlineCapacity &&
               alignof(Decayed) <= MoveOnlyFunctionInlineAlignment;
    }

    template <typename Decayed>
    static constexpr Operations inlineOperationsFor{
        .invoke = [](void* storage, Args&&... args) -> Result {
            return (*static_cast<Decayed*>(storage))(std::forward<Args>(args)...);
        },
        .relocate = [](void* sourceStorage, void* destinationStorage) noexcept {
            Decayed* const source = static_cast<Decayed*>(sourceStorage);
            ::new (destinationStorage) Decayed(std::move(*source));
            std::destroy_at(source);
        },
        // std::destroy_at rather than an explicit ~Decayed(): MSVC rejects the
        // pseudo-destructor spelling when Decayed is a template parameter naming a
        // closure type.
        .destroy = [](void* storage) noexcept { std::destroy_at(static_cast<Decayed*>(storage)); },
    };

    template <typename Decayed>
    static constexpr Operations heapOperationsFor{
        .invoke = [](void* storage, Args&&... args) -> Result {
            return (*static_cast<Decayed*>(*static_cast<HeapTargetPointer*>(storage)))(
                std::forward<Args>(args)...);
        },
        // Only the pointer moves, and ownership moves with it -- the target itself stays
        // put, so a queued work item's address is stable and the move neither allocates
        // nor throws. Nothing is deleted here; the destination is now the sole owner.
        .relocate = [](void* sourceStorage, void* destinationStorage) noexcept {
            ::new (destinationStorage) HeapTargetPointer(*static_cast<HeapTargetPointer*>(sourceStorage));
        },
        .destroy = [](void* storage) noexcept {
            delete static_cast<Decayed*>(*static_cast<HeapTargetPointer*>(storage));
        },
    };

    void adoptFrom(MoveOnlyFunction& other) noexcept
    {
        if (other.m_operations == nullptr)
        {
            return;
        }
        // relocate ends the source target's lifetime itself, so only the empty marker is
        // left to clear here.
        other.m_operations->relocate(other.m_storage, m_storage);
        m_operations = other.m_operations;
        other.m_operations = nullptr;
    }

    void destroyTarget() noexcept
    {
        if (m_operations != nullptr)
        {
            m_operations->destroy(m_storage);
            m_operations = nullptr;
        }
    }

    alignas(MoveOnlyFunctionInlineAlignment) unsigned char m_storage[MoveOnlyFunctionInlineCapacity]{};
    const Operations* m_operations = nullptr;
};

} // namespace Tina::Core
