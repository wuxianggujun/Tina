#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/platform/Input.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/Window.hpp>
#include <tina/ui/UIHitTest.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>

#include <array>
#include <bitset>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace Tina::UI {

class UIContext;

namespace Detail {

class UIRoutedPointerEventAccess;

} // namespace Detail

enum class UIRoutedPointerEventKind : u8 {
    Move,
    ButtonDown,
    ButtonUp,
    Wheel,
};

enum class UIEventPhase : u8 {
    Capture,
    Target,
    Bubble,
};

enum class UIEventPhaseMask : u8 {
    None = 0,
    Capture = 1u << 0u,
    Target = 1u << 1u,
    Bubble = 1u << 2u,
    All = (1u << 0u) | (1u << 1u) | (1u << 2u),
};

[[nodiscard]] constexpr u8 eventPhaseMaskValue(UIEventPhaseMask value) noexcept
{
    return static_cast<std::underlying_type_t<UIEventPhaseMask>>(value);
}

[[nodiscard]] constexpr UIEventPhaseMask operator|(
    UIEventPhaseMask left,
    UIEventPhaseMask right) noexcept
{
    return static_cast<UIEventPhaseMask>(
        eventPhaseMaskValue(left) | eventPhaseMaskValue(right));
}

[[nodiscard]] constexpr UIEventPhaseMask operator&(
    UIEventPhaseMask left,
    UIEventPhaseMask right) noexcept
{
    return static_cast<UIEventPhaseMask>(
        eventPhaseMaskValue(left) & eventPhaseMaskValue(right));
}

constexpr UIEventPhaseMask& operator|=(
    UIEventPhaseMask& left,
    UIEventPhaseMask right) noexcept
{
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool hasEventPhase(
    UIEventPhaseMask phases,
    UIEventPhaseMask phase) noexcept
{
    return (phases & phase) != UIEventPhaseMask::None;
}

// One backend-normalized pointer transition. Position and delta remain in
// window logical coordinates; transitionOrdinal identifies the source entry in
// the matching PlatformFrameId while sourceSequence preserves platform order.
struct UIPointerInputEvent final {
    Platform::PlatformFrameId platformFrame{};
    usize transitionOrdinal = 0;
    u64 sourceSequence = 0;
    Platform::WindowId window{};
    Platform::PointerId pointer = Platform::PrimaryPointerId;
    UIRoutedPointerEventKind kind = UIRoutedPointerEventKind::Move;
    UILogicalPoint position{};
    UILogicalPoint delta{};
    Platform::PointerButton button = Platform::PointerButton::Primary;

    auto operator<=>(const UIPointerInputEvent&) const = default;
};

// Mutable callback-scope event. Input and route identity remain stable for one
// dispatch; only propagation, transition-consumption, continuous-control claim,
// and cancelable default-action decisions may change.
class UIRoutedPointerEvent final {
public:
    UIRoutedPointerEvent(const UIRoutedPointerEvent&) = delete;
    UIRoutedPointerEvent& operator=(const UIRoutedPointerEvent&) = delete;
    UIRoutedPointerEvent(UIRoutedPointerEvent&&) = delete;
    UIRoutedPointerEvent& operator=(UIRoutedPointerEvent&&) = delete;

    [[nodiscard]] constexpr const UIPointerInputEvent& input() const noexcept
    {
        return m_input;
    }

    [[nodiscard]] constexpr UIEventPhase currentPhase() const noexcept
    {
        return m_currentPhase;
    }

    [[nodiscard]] constexpr UINodeId currentNode() const noexcept
    {
        return m_currentNode;
    }

    [[nodiscard]] constexpr UINodeId targetNode() const noexcept
    {
        return m_targetNode;
    }

    [[nodiscard]] constexpr UINodeId rootNode() const noexcept
    {
        return m_rootNode;
    }

    constexpr void stopPropagation() noexcept
    {
        m_propagationStopped = true;
    }

    constexpr void stopImmediatePropagation() noexcept
    {
        m_immediatePropagationStopped = true;
        m_propagationStopped = true;
    }

    constexpr void consumeInputTransition() noexcept
    {
        m_inputTransitionConsumed = true;
    }

    // Prevents the cancelable Widget default action for this route. This is
    // independent from propagation, transition consumption, and control claim.
    constexpr void preventDefaultAction() noexcept
    {
        m_defaultActionPrevented = true;
    }

    // Requests frame-local ownership of one button on this event's Window and
    // Pointer. This is independent from transition consumption: Runtime only
    // publishes the request when the final Platform snapshot still holds the
    // button. Repeated valid requests are idempotent; invalid enum values fail.
    [[nodiscard]] constexpr bool claimPointerButton(
        Platform::PointerButton button) noexcept
    {
        const usize index = static_cast<usize>(button);
        if (index >= m_claimedPointerButtons.size())
        {
            return false;
        }

        m_claimedPointerButtons[index] = true;
        return true;
    }

    [[nodiscard]] constexpr bool isPropagationStopped() const noexcept
    {
        return m_propagationStopped;
    }

    [[nodiscard]] constexpr bool isImmediatePropagationStopped() const noexcept
    {
        return m_immediatePropagationStopped;
    }

    [[nodiscard]] constexpr bool isInputTransitionConsumed() const noexcept
    {
        return m_inputTransitionConsumed;
    }

    [[nodiscard]] constexpr bool isDefaultActionPrevented() const noexcept
    {
        return m_defaultActionPrevented;
    }

private:
    friend class UIContext;
    friend class Detail::UIRoutedPointerEventAccess;

    explicit constexpr UIRoutedPointerEvent(UIPointerInputEvent input) noexcept
        : m_input(std::move(input))
    {
    }

    constexpr void setRouteState(
        UIEventPhase phase,
        UINodeId current,
        UINodeId target,
        UINodeId root) noexcept
    {
        m_currentPhase = phase;
        m_currentNode = current;
        m_targetNode = target;
        m_rootNode = root;
    }

    UIPointerInputEvent m_input{};
    UIEventPhase m_currentPhase = UIEventPhase::Target;
    UINodeId m_currentNode{};
    UINodeId m_targetNode{};
    UINodeId m_rootNode{};
    bool m_propagationStopped = false;
    bool m_immediatePropagationStopped = false;
    bool m_inputTransitionConsumed = false;
    bool m_defaultActionPrevented = false;
    std::bitset<Platform::PointerButtonCount> m_claimedPointerButtons{};
};

namespace Detail {

// Narrow implementation seam used by UIContext::Impl without exposing mutable
// route identity on the public event surface.
class UIRoutedPointerEventAccess final {
public:
    [[nodiscard]] static constexpr UIRoutedPointerEvent Create(
        UIPointerInputEvent input) noexcept
    {
        return UIRoutedPointerEvent(std::move(input));
    }

    static constexpr void setRouteState(
        UIRoutedPointerEvent& event,
        UIEventPhase phase,
        UINodeId current,
        UINodeId target,
        UINodeId root) noexcept
    {
        event.setRouteState(phase, current, target, root);
    }

    [[nodiscard]] static constexpr const std::bitset<Platform::PointerButtonCount>&
    claimedPointerButtons(const UIRoutedPointerEvent& event) noexcept
    {
        return event.m_claimedPointerButtons;
    }
};

} // namespace Detail

// Move-only fixed-inline callback. Callables larger or more strictly aligned
// than InlineStorageBytes are rejected at compile time; there is no allocator
// and therefore no heap fallback.
class UIRoutedPointerCallback final {
public:
    static constexpr usize InlineStorageBytes = 48;

private:
    // Declare the constructor constraint before its first use. MSVC accepts a
    // later class-scope declaration, while GCC correctly requires the name to
    // be visible at the requires-clause point.
    template <typename Callable, typename Source>
    static constexpr bool CanStoreCallable =
        !std::is_same_v<Callable, UIRoutedPointerCallback>
        && sizeof(Callable) <= InlineStorageBytes
        && alignof(Callable) <= alignof(std::max_align_t)
        && std::is_nothrow_constructible_v<Callable, Source&&>
        && std::is_nothrow_move_constructible_v<Callable>
        && std::is_nothrow_destructible_v<Callable>
        && std::is_nothrow_invocable_r_v<void, Callable&, UIRoutedPointerEvent&>;

public:
    UIRoutedPointerCallback() noexcept = default;

    template <typename Source>
        requires CanStoreCallable<std::decay_t<Source>, Source>
    explicit UIRoutedPointerCallback(Source&& source) noexcept
    {
        using Callable = std::decay_t<Source>;
        std::construct_at(
            reinterpret_cast<Callable*>(m_storage.data()),
            std::forward<Source>(source));
        m_invoke = &invokeCallable<Callable>;
        m_move = &moveCallable<Callable>;
        m_destroy = &destroyCallable<Callable>;
    }

    ~UIRoutedPointerCallback() noexcept
    {
        reset();
    }

    UIRoutedPointerCallback(const UIRoutedPointerCallback&) = delete;
    UIRoutedPointerCallback& operator=(const UIRoutedPointerCallback&) = delete;

    UIRoutedPointerCallback(UIRoutedPointerCallback&& other) noexcept
    {
        moveFrom(other);
    }

    UIRoutedPointerCallback& operator=(UIRoutedPointerCallback&& other) noexcept
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
        // Clear the visible operations before entering user destruction. A
        // callable destructor may release another UI owner/token and re-enter
        // listener cleanup; observing this callback as still live would make
        // that path destroy the same callable twice.
        const DestroyOperation destroy = std::exchange(m_destroy, nullptr);
        m_invoke = nullptr;
        m_move = nullptr;
        if (destroy != nullptr) {
            destroy(m_storage.data());
        }
    }

    void operator()(UIRoutedPointerEvent& event) noexcept
    {
        if (m_invoke != nullptr) {
            m_invoke(m_storage.data(), event);
        }
    }

private:
    using InvokeOperation = void (*)(std::byte*, UIRoutedPointerEvent&) noexcept;
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
        UIRoutedPointerEvent& event) noexcept
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

    void moveFrom(UIRoutedPointerCallback& other) noexcept
    {
        if (!other.hasValue()) {
            return;
        }

        const InvokeOperation invoke = other.m_invoke;
        const MoveOperation move = other.m_move;
        const DestroyOperation destroy = other.m_destroy;
        other.clearOperations();

        // Moving the callable also destroys its moved-from object. Make the
        // source callback empty before either user operation can re-enter UI.
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

struct UIRoutedPointerListenerDesc final {
    UINodeId node{};
    UIRoutedPointerEventKind kind = UIRoutedPointerEventKind::Move;
    UIEventPhaseMask phases = UIEventPhaseMask::All;

    auto operator<=>(const UIRoutedPointerListenerDesc&) const = default;
};

struct UIPointerRouteResult final {
    UIPointerHitQueryResult pointQuery{};
    // Requested buttons are interpreted with the routed input's Window and
    // Pointer identity by the Runtime route-result producer.
    std::bitset<Platform::PointerButtonCount> claimedPointerButtons{};
    usize routeDepth = 0;
    usize listenerInvocationCount = 0;
    bool consumed = false;
    bool stopped = false;
    bool targetInvalidated = false;

    [[nodiscard]] constexpr bool hasRoutedTarget() const noexcept
    {
        return pointQuery.hasTarget() && routeDepth != 0 && !targetInvalidated;
    }
};

} // namespace Tina::UI
