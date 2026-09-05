#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIPaint.hpp>

#include <array>
#include <compare>
#include <cstddef>
// std::construct_at / std::destroy_at are used below. <new> happens to supply them
// on the current toolchain, but they are specified in <memory>; relying on the
// transitive path leaves this public header dependent on an implementation detail.
#include <memory>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Tina::UI {

struct UITextEditPaint final {
    UIStraightSrgba8Color hoveredBackgroundColor{};
    UIStraightSrgba8Color pressedBackgroundColor{};
    UIStraightSrgba8Color focusedBackgroundColor{};
    UIStraightSrgba8Color disabledBackgroundColor{};
    UIStraightSrgba8Color focusedBorderColor{};
    UIStraightSrgba8Color selectionBackgroundColor{
        .red = 42,
        .green = 112,
        .blue = 190,
        .alpha = 190,
    };
    UIStraightSrgba8Color caretColor{
        .red = 255,
        .green = 255,
        .blue = 255,
        .alpha = 255,
    };

    auto operator<=>(const UITextEditPaint&) const = default;
};

enum class UITextEditWrapMode : u8 {
    NoWrap,
    SoftWrap,
};

// Multiline storage and public offsets remain scalar-indexed; editing and hit
// testing align those offsets to grapheme-cluster boundaries.
struct UITextEditMultilineConfig final {
    bool enabled = false;
    UITextEditWrapMode wrapMode = UITextEditWrapMode::NoWrap;
    // Zero preserves the context text-arena limit as the only byte limit.
    usize maximumBytes = 0;
    // Zero preserves the single-line default. When enabled, this is the maximum
    // number of visual rows, including rows created by soft wrapping.
    u32 maximumVisualLines = 0;
    bool verticalScrollEnabled = true;
    float wheelStep = 24.0F;

    auto operator<=>(const UITextEditMultilineConfig&) const = default;
};

// Selection offsets count Unicode scalar values in the committed UTF-8 text.
// Accepted and generated positions align to grapheme-cluster boundaries.
// BiDi- and shaping-aware visual cursor movement remains deferred.
struct UITextSelection final {
    u32 anchorCodepoint = 0;
    u32 caretCodepoint = 0;

    [[nodiscard]] constexpr bool isCollapsed() const noexcept
    {
        return anchorCodepoint == caretCodepoint;
    }

    auto operator<=>(const UITextSelection&) const = default;
};

// Fired after a TextEdit's committed value changes. The callback runs on the
// UI owner thread and remains synchronous with the mutation that caused it.
//
// `text` is a snapshot taken when the event was raised, not a view of the live
// widget storage. It stays valid and unchanged for the whole callback even if that
// callback calls setText, destroys the node, or triggers a nested text event, and
// it expires as soon as the callback returns -- callers must not retain it.
//
// If the bounded text storage cannot hold the snapshot, the event is dropped
// rather than delivered with a partial or dangling view.
struct UITextChangedEvent final {
    UINodeId textEdit{};
    std::string_view text{};
    UITextSelection selection{};
    bool userInitiated = false;
    Platform::PlatformFrameId platformFrame{};
    u64 sourceSequence = 0;

    auto operator<=>(const UITextChangedEvent&) const = default;
};

// Fired by a single-line TextEdit when the user submits it (Enter or keypad
// Enter). Multiline TextEdits insert a line feed instead and do not emit this.
//
// `text` follows the same snapshot rule as UITextChangedEvent: stable across a
// reentrant callback, invalid once the callback returns. A submit handler that
// clears the field is the common case, and it reads its own event text safely.
struct UITextSubmitEvent final {
    UINodeId textEdit{};
    std::string_view text{};
    Platform::PlatformFrameId platformFrame{};
    u64 sourceSequence = 0;

    auto operator<=>(const UITextSubmitEvent&) const = default;
};

enum class UITextEditCommand : u8 {
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    MoveHome,
    MoveEnd,
    Backspace,
    Delete,
    SelectAll,
    // Enter/Keypad Enter. Single-line edits submit; multiline edits insert LF.
    Submit,
};

// Move-only fixed-inline callback for TextEdit value changes. Keeping the
// callable inline makes callback registration bounded by the UI context's
// preallocated node capacity and avoids an implicit heap-backed API contract.
class UITextChangedCallback final {
  public:
    static constexpr usize InlineStorageBytes = 48;

  private:
    template <typename Callable, typename Source>
    static constexpr bool CanStoreCallable =
        !std::is_same_v<Callable, UITextChangedCallback> &&
        sizeof(Callable) <= InlineStorageBytes &&
        alignof(Callable) <= alignof(std::max_align_t) &&
        std::is_nothrow_constructible_v<Callable, Source&&> &&
        std::is_nothrow_move_constructible_v<Callable> &&
        std::is_nothrow_destructible_v<Callable> &&
        std::is_nothrow_invocable_r_v<void, Callable&, const UITextChangedEvent&>;

  public:
    UITextChangedCallback() noexcept = default;

    template <typename Source>
        requires (!std::is_same_v<std::decay_t<Source>, UITextChangedCallback>) &&
                 CanStoreCallable<std::decay_t<Source>, Source>
    explicit UITextChangedCallback(Source&& source) noexcept
    {
        using Callable = std::decay_t<Source>;
        std::construct_at(reinterpret_cast<Callable*>(m_storage.data()),
                          std::forward<Source>(source));
        m_invoke = &invokeCallable<Callable>;
        m_move = &moveCallable<Callable>;
        m_destroy = &destroyCallable<Callable>;
    }

    ~UITextChangedCallback() noexcept { reset(); }

    UITextChangedCallback(const UITextChangedCallback&) = delete;
    UITextChangedCallback& operator=(const UITextChangedCallback&) = delete;

    UITextChangedCallback(UITextChangedCallback&& other) noexcept { moveFrom(other); }

    UITextChangedCallback& operator=(UITextChangedCallback&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            moveFrom(other);
        }
        return *this;
    }

    [[nodiscard]] bool hasValue() const noexcept { return m_invoke != nullptr; }
    explicit operator bool() const noexcept { return hasValue(); }

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

    void operator()(const UITextChangedEvent& event) noexcept
    {
        if (m_invoke != nullptr)
        {
            m_invoke(m_storage.data(), event);
        }
    }

  private:
    using InvokeOperation = void (*)(std::byte*, const UITextChangedEvent&) noexcept;
    using MoveOperation = void (*)(std::byte*, std::byte*) noexcept;
    using DestroyOperation = void (*)(std::byte*) noexcept;

    template <typename Callable>
    [[nodiscard]] static Callable& callableAt(std::byte* storage) noexcept
    {
        return *std::launder(reinterpret_cast<Callable*>(storage));
    }

    template <typename Callable>
    static void invokeCallable(std::byte* storage, const UITextChangedEvent& event) noexcept
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

    void moveFrom(UITextChangedCallback& other) noexcept
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

// Move-only fixed-inline callback for single-line TextEdit submission.
class UITextSubmitCallback final {
  public:
    static constexpr usize InlineStorageBytes = 48;

  private:
    template <typename Callable, typename Source>
    static constexpr bool CanStoreCallable =
        !std::is_same_v<Callable, UITextSubmitCallback> &&
        sizeof(Callable) <= InlineStorageBytes &&
        alignof(Callable) <= alignof(std::max_align_t) &&
        std::is_nothrow_constructible_v<Callable, Source&&> &&
        std::is_nothrow_move_constructible_v<Callable> &&
        std::is_nothrow_destructible_v<Callable> &&
        std::is_nothrow_invocable_r_v<void, Callable&, const UITextSubmitEvent&>;

  public:
    UITextSubmitCallback() noexcept = default;

    template <typename Source>
        requires (!std::is_same_v<std::decay_t<Source>, UITextSubmitCallback>) &&
                 CanStoreCallable<std::decay_t<Source>, Source>
    explicit UITextSubmitCallback(Source&& source) noexcept
    {
        using Callable = std::decay_t<Source>;
        std::construct_at(reinterpret_cast<Callable*>(m_storage.data()),
                          std::forward<Source>(source));
        m_invoke = &invokeCallable<Callable>;
        m_move = &moveCallable<Callable>;
        m_destroy = &destroyCallable<Callable>;
    }

    ~UITextSubmitCallback() noexcept { reset(); }

    UITextSubmitCallback(const UITextSubmitCallback&) = delete;
    UITextSubmitCallback& operator=(const UITextSubmitCallback&) = delete;

    UITextSubmitCallback(UITextSubmitCallback&& other) noexcept { moveFrom(other); }

    UITextSubmitCallback& operator=(UITextSubmitCallback&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            moveFrom(other);
        }
        return *this;
    }

    [[nodiscard]] bool hasValue() const noexcept { return m_invoke != nullptr; }
    explicit operator bool() const noexcept { return hasValue(); }

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

    void operator()(const UITextSubmitEvent& event) noexcept
    {
        if (m_invoke != nullptr)
        {
            m_invoke(m_storage.data(), event);
        }
    }

  private:
    using InvokeOperation = void (*)(std::byte*, const UITextSubmitEvent&) noexcept;
    using MoveOperation = void (*)(std::byte*, std::byte*) noexcept;
    using DestroyOperation = void (*)(std::byte*) noexcept;

    template <typename Callable>
    [[nodiscard]] static Callable& callableAt(std::byte* storage) noexcept
    {
        return *std::launder(reinterpret_cast<Callable*>(storage));
    }

    template <typename Callable>
    static void invokeCallable(std::byte* storage, const UITextSubmitEvent& event) noexcept
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

    void moveFrom(UITextSubmitCallback& other) noexcept
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
