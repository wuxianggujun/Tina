#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/ui/UINodeId.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace Tina::UI {

class UIContext;
class UITreeUpdater;

// Strong identities for existing retained nodes. The underlying nodes remain
// owned by UIRootOwner and are never duplicated into a second UI tree.
class UIFlowLayerId final {
  public:
    constexpr UIFlowLayerId() noexcept = default;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return m_node.hasValue();
    }

    [[nodiscard]] constexpr UINodeId nodeId() const noexcept
    {
        return m_node;
    }

    explicit constexpr operator bool() const noexcept
    {
        return hasValue();
    }

    auto operator<=>(const UIFlowLayerId&) const = default;

  private:
    friend class UIContext;
    friend class UITreeUpdater;

    explicit constexpr UIFlowLayerId(UINodeId node) noexcept : m_node(node) {}

    UINodeId m_node{};
};

class UIFlowScreenId final {
  public:
    constexpr UIFlowScreenId() noexcept = default;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return m_node.hasValue();
    }

    [[nodiscard]] constexpr UINodeId nodeId() const noexcept
    {
        return m_node;
    }

    explicit constexpr operator bool() const noexcept
    {
        return hasValue();
    }

    auto operator<=>(const UIFlowScreenId&) const = default;

  private:
    friend class UIContext;
    friend class UITreeUpdater;

    explicit constexpr UIFlowScreenId(UINodeId node) noexcept : m_node(node) {}

    UINodeId m_node{};
};

// Small conventional action set routed to the topmost active Screen. Product
// input mapping remains in Runtime so the retained UI contract stays backend-neutral.
enum class UIFlowAction : u8 {
    Back,
    Confirm,
};

enum class UIFlowActionSource : u8 {
    Keyboard,
    Gamepad,
};

// Per-window, single-local-user input category used by product action prompts.
// Keyboard and pointer share one category because Desktop prompts normally
// present them as one control scheme. Multi-user assignment remains outside
// this contract.
enum class UIFlowInputDevice : u8 {
    KeyboardMouse,
    Gamepad,
};

struct UIFlowInputDeviceState final {
    UIFlowInputDevice device = UIFlowInputDevice::KeyboardMouse;
    std::optional<Platform::GamepadId> gamepad{};
    Platform::PlatformFrameId platformFrame{};
    u64 sourceSequence = 0;
    // Changes only when the device category or active Gamepad identity changes.
    u64 revision = 0;

    auto operator<=>(const UIFlowInputDeviceState&) const = default;
};

struct UIFlowActionEvent final {
    UIFlowScreenId screen{};
    UIFlowAction action = UIFlowAction::Back;
    UIFlowActionSource source = UIFlowActionSource::Keyboard;
    Platform::PlatformFrameId platformFrame{};
    u64 sourceSequence = 0;

    auto operator<=>(const UIFlowActionEvent&) const = default;
};

// Move-only fixed-inline callback. The callable is owned by its registered
// Screen and cannot fall back to a heap allocation.
class UIFlowActionCallback final {
  public:
    static constexpr usize InlineStorageBytes = 48;

  private:
    template <typename Callable, typename Source>
    static constexpr bool CanStoreCallable =
        !std::is_same_v<Callable, UIFlowActionCallback> &&
        sizeof(Callable) <= InlineStorageBytes &&
        alignof(Callable) <= alignof(std::max_align_t) &&
        std::is_nothrow_constructible_v<Callable, Source&&> &&
        std::is_nothrow_move_constructible_v<Callable> &&
        std::is_nothrow_destructible_v<Callable> &&
        std::is_nothrow_invocable_r_v<void, Callable&, const UIFlowActionEvent&>;

  public:
    UIFlowActionCallback() noexcept = default;

    template <typename Source>
        requires CanStoreCallable<std::decay_t<Source>, Source>
    explicit UIFlowActionCallback(Source&& source) noexcept
    {
        using Callable = std::decay_t<Source>;
        std::construct_at(reinterpret_cast<Callable*>(m_storage.data()),
                          std::forward<Source>(source));
        m_invoke = &invokeCallable<Callable>;
        m_move = &moveCallable<Callable>;
        m_destroy = &destroyCallable<Callable>;
    }

    ~UIFlowActionCallback() noexcept
    {
        reset();
    }

    UIFlowActionCallback(const UIFlowActionCallback&) = delete;
    UIFlowActionCallback& operator=(const UIFlowActionCallback&) = delete;

    UIFlowActionCallback(UIFlowActionCallback&& other) noexcept
    {
        moveFrom(other);
    }

    UIFlowActionCallback& operator=(UIFlowActionCallback&& other) noexcept
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

    void operator()(const UIFlowActionEvent& event) noexcept
    {
        if (m_invoke != nullptr)
        {
            m_invoke(m_storage.data(), event);
        }
    }

  private:
    using InvokeOperation = void (*)(std::byte*, const UIFlowActionEvent&) noexcept;
    using MoveOperation = void (*)(std::byte*, std::byte*) noexcept;
    using DestroyOperation = void (*)(std::byte*) noexcept;

    template <typename Callable>
    [[nodiscard]] static Callable& callableAt(std::byte* storage) noexcept
    {
        return *std::launder(reinterpret_cast<Callable*>(storage));
    }

    template <typename Callable>
    static void invokeCallable(std::byte* storage, const UIFlowActionEvent& event) noexcept
    {
        callableAt<Callable>(storage)(event);
    }

    template <typename Callable>
    static void moveCallable(std::byte* source, std::byte* destination) noexcept
    {
        std::construct_at(reinterpret_cast<Callable*>(destination),
                          std::move(callableAt<Callable>(source)));
        std::destroy_at(&callableAt<Callable>(source));
    }

    template <typename Callable>
    static void destroyCallable(std::byte* storage) noexcept
    {
        std::destroy_at(&callableAt<Callable>(storage));
    }

    void moveFrom(UIFlowActionCallback& other) noexcept
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

struct UIFlowActionRouteResult final {
    bool consumed = false;
    bool invoked = false;
    UIFlowScreenId screen{};
};

struct UIFlowStatistics final {
    usize layerCapacity = 0;
    usize registeredLayerCount = 0;
    usize layerHighWater = 0;
    usize screenCapacity = 0;
    usize registeredScreenCount = 0;
    usize screenHighWater = 0;
    usize stackedScreenCount = 0;
    usize stackHighWater = 0;
    usize registeredActionCount = 0;
    usize actionHighWater = 0;
    usize actionInvocationCount = 0;
    // Registration pool exhaustion and visibility-publication preflight rejection.
    usize capacityFailureCount = 0;
};

} // namespace Tina::UI
