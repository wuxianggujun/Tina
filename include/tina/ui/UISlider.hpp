#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIPaint.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace Tina::UI {

// Slider-specific chrome. The retained node remains the full hit target while
// trackThickness and thumbExtent define centered visual geometry inside it.
// All primitives continue through the existing committed paint pipeline.
struct UISliderPaint final {
    UIStraightSrgba8Color trackColor{};
    UIStraightSrgba8Color filledTrackColor{};
    UIStraightSrgba8Color thumbColor{};
    UIStraightSrgba8Color hoveredThumbColor{};
    UIStraightSrgba8Color draggingThumbColor{};
    UIStraightSrgba8Color focusedThumbColor{};
    float contentInset = 6.0F;
    float trackThickness = 4.0F;
    float thumbExtent = 12.0F;

    auto operator<=>(const UISliderPaint&) const = default;
};

// Fired when Slider value actually changes through setValue, drag, or a
// range/step re-clamp.
struct UISliderChangeEvent final {
    UINodeId sliderNode{};
    float value = 0.0F;
    Platform::PlatformFrameId platformFrame{};
    u64 sourceSequence = 0;

    auto operator<=>(const UISliderChangeEvent&) const = default;
};

// Move-only fixed-inline callback (same storage constraints as Button actions).
class UISliderChangeCallback final {
  public:
    static constexpr usize InlineStorageBytes = 48;

  private:
    template <typename Callable, typename Source>
    static constexpr bool CanStoreCallable =
        !std::is_same_v<Callable, UISliderChangeCallback> && sizeof(Callable) <= InlineStorageBytes &&
        alignof(Callable) <= alignof(std::max_align_t) && std::is_nothrow_constructible_v<Callable, Source&&> &&
        std::is_nothrow_move_constructible_v<Callable> && std::is_nothrow_destructible_v<Callable> &&
        std::is_nothrow_invocable_r_v<void, Callable&, const UISliderChangeEvent&>;

  public:
    UISliderChangeCallback() noexcept = default;

    template <typename Source>
        requires CanStoreCallable<std::decay_t<Source>, Source>
    explicit UISliderChangeCallback(Source&& source) noexcept
    {
        using Callable = std::decay_t<Source>;
        std::construct_at(reinterpret_cast<Callable*>(m_storage.data()), std::forward<Source>(source));
        m_invoke = &invokeCallable<Callable>;
        m_move = &moveCallable<Callable>;
        m_destroy = &destroyCallable<Callable>;
    }

    ~UISliderChangeCallback() noexcept
    {
        reset();
    }

    UISliderChangeCallback(const UISliderChangeCallback&) = delete;
    UISliderChangeCallback& operator=(const UISliderChangeCallback&) = delete;

    UISliderChangeCallback(UISliderChangeCallback&& other) noexcept
    {
        moveFrom(other);
    }

    UISliderChangeCallback& operator=(UISliderChangeCallback&& other) noexcept
    {
        if (this != &other)
        {
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
        if (destroy != nullptr)
        {
            destroy(m_storage.data());
        }
    }

    void operator()(const UISliderChangeEvent& event) noexcept
    {
        if (m_invoke != nullptr)
        {
            m_invoke(m_storage.data(), event);
        }
    }

  private:
    using InvokeOperation = void (*)(std::byte*, const UISliderChangeEvent&) noexcept;
    using MoveOperation = void (*)(std::byte*, std::byte*) noexcept;
    using DestroyOperation = void (*)(std::byte*) noexcept;

    template <typename Callable>
    [[nodiscard]] static Callable& callableAt(std::byte* storage) noexcept
    {
        return *std::launder(reinterpret_cast<Callable*>(storage));
    }

    template <typename Callable>
    static void invokeCallable(std::byte* storage, const UISliderChangeEvent& event) noexcept
    {
        callableAt<Callable>(storage)(event);
    }

    template <typename Callable>
    static void moveCallable(std::byte* source, std::byte* destination) noexcept
    {
        std::construct_at(reinterpret_cast<Callable*>(destination), std::move(callableAt<Callable>(source)));
        std::destroy_at(&callableAt<Callable>(source));
    }

    template <typename Callable>
    static void destroyCallable(std::byte* storage) noexcept
    {
        std::destroy_at(&callableAt<Callable>(storage));
    }

    void moveFrom(UISliderChangeCallback& other) noexcept
    {
        if (!other.hasValue())
        {
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
