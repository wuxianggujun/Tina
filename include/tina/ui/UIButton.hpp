#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIPaint.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace Tina::UI {

// Interaction-state overrides for a Button. UIBoxPaint remains the normal-state
// escape hatch. A zero-alpha background override falls back to the next state,
// with disabled > pressed > hovered > focused > normal precedence. Focused
// borders do not replace the fill; pressed chrome collapses the UIBoxPaint
// shadow and reverses its dual-tone border to provide deterministic depth.
struct UIButtonPaint final {
    UIStraightSrgba8Color hoveredBackgroundColor{};
    UIStraightSrgba8Color pressedBackgroundColor{};
    UIStraightSrgba8Color focusedBackgroundColor{};
    UIStraightSrgba8Color disabledBackgroundColor{};
    UIStraightSrgba8Color focusedBorderColor{};

    auto operator<=>(const UIButtonPaint&) const = default;
};

enum class UIButtonActivationSource : u8 {
    PrimaryPointer,
    // Keyboard Accept (Enter/Space) and Gamepad Accept (South) activate the
    // Button that currently owns default-action focus (set by pointer input
    // or keyboard focus traversal).
    Keyboard,
    Gamepad,
    // Platform accessibility adapters invoke controls through the owner-thread
    // UIAccessibilityAction seam rather than synthesizing physical input.
    Accessibility,
};

struct UIButtonActionEvent final {
    UINodeId buttonNode{};
    UIButtonActivationSource source = UIButtonActivationSource::PrimaryPointer;
    Platform::PlatformFrameId platformFrame{};
    u64 sourceSequence = 0;

    auto operator<=>(const UIButtonActionEvent&) const = default;
};

// Move-only fixed-inline callback for retained Button actions. Callables larger
// or more strictly aligned than InlineStorageBytes are rejected at compile
// time; there is no allocator and therefore no heap fallback.
class UIButtonActionCallback final {
public:
    static constexpr usize InlineStorageBytes = 48;

private:
    template <typename Callable, typename Source>
    static constexpr bool CanStoreCallable =
        !std::is_same_v<Callable, UIButtonActionCallback>
        && sizeof(Callable) <= InlineStorageBytes
        && alignof(Callable) <= alignof(std::max_align_t)
        && std::is_nothrow_constructible_v<Callable, Source&&>
        && std::is_nothrow_move_constructible_v<Callable>
        && std::is_nothrow_destructible_v<Callable>
        && std::is_nothrow_invocable_r_v<void, Callable&, const UIButtonActionEvent&>;

public:
    UIButtonActionCallback() noexcept = default;

    template <typename Source>
        requires CanStoreCallable<std::decay_t<Source>, Source>
    explicit UIButtonActionCallback(Source&& source) noexcept
    {
        using Callable = std::decay_t<Source>;
        std::construct_at(
            reinterpret_cast<Callable*>(m_storage.data()),
            std::forward<Source>(source));
        m_invoke = &invokeCallable<Callable>;
        m_move = &moveCallable<Callable>;
        m_destroy = &destroyCallable<Callable>;
    }

    ~UIButtonActionCallback() noexcept
    {
        reset();
    }

    UIButtonActionCallback(const UIButtonActionCallback&) = delete;
    UIButtonActionCallback& operator=(const UIButtonActionCallback&) = delete;

    UIButtonActionCallback(UIButtonActionCallback&& other) noexcept
    {
        moveFrom(other);
    }

    UIButtonActionCallback& operator=(UIButtonActionCallback&& other) noexcept
    {
        if (this != &other) {
            reset();
            moveFrom(other);
        }
        return *this;
    }

    [[nodiscard]] bool hasValue() const noexcept
    {
        return m_invoke != nullptr;
    }

    explicit operator bool() const noexcept
    {
        return hasValue();
    }

    void reset() noexcept
    {
        const DestroyOperation destroy = std::exchange(m_destroy, nullptr);
        m_invoke = nullptr;
        m_move = nullptr;
        if (destroy != nullptr) {
            destroy(m_storage.data());
        }
    }

    void operator()(const UIButtonActionEvent& event) noexcept
    {
        if (m_invoke != nullptr) {
            m_invoke(m_storage.data(), event);
        }
    }

private:
    using InvokeOperation = void (*)(std::byte*, const UIButtonActionEvent&) noexcept;
    using MoveOperation = void (*)(std::byte*, std::byte*) noexcept;
    using DestroyOperation = void (*)(std::byte*) noexcept;

    template <typename Callable>
    [[nodiscard]] static Callable& callableAt(std::byte* storage) noexcept
    {
        return *std::launder(reinterpret_cast<Callable*>(storage));
    }

    template <typename Callable>
    static void invokeCallable(
        std::byte* storage,
        const UIButtonActionEvent& event) noexcept
    {
        callableAt<Callable>(storage)(event);
    }

    template <typename Callable>
    static void moveCallable(std::byte* source, std::byte* destination) noexcept
    {
        std::construct_at(
            reinterpret_cast<Callable*>(destination),
            std::move(callableAt<Callable>(source)));
        std::destroy_at(&callableAt<Callable>(source));
    }

    template <typename Callable>
    static void destroyCallable(std::byte* storage) noexcept
    {
        std::destroy_at(&callableAt<Callable>(storage));
    }

    void moveFrom(UIButtonActionCallback& other) noexcept
    {
        if (!other.hasValue()) {
            return;
        }

        const InvokeOperation invoke = other.m_invoke;
        const MoveOperation move = other.m_move;
        const DestroyOperation destroy = other.m_destroy;
        other.clearOperations();

        move(other.m_storage.data(), m_storage.data());
        m_invoke = invoke;
        m_move = move;
        m_destroy = destroy;
    }

    void clearOperations() noexcept
    {
        m_invoke = nullptr;
        m_move = nullptr;
        m_destroy = nullptr;
    }

    alignas(std::max_align_t) std::array<std::byte, InlineStorageBytes> m_storage{};
    InvokeOperation m_invoke = nullptr;
    MoveOperation m_move = nullptr;
    DestroyOperation m_destroy = nullptr;
};

} // namespace Tina::UI
