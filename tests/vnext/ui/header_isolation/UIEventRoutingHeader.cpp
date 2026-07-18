#include <tina/ui/UIEventRouting.hpp>

#include <type_traits>

namespace {

struct NoexceptPointerListener final {
    void operator()(Tina::UI::UIRoutedPointerEvent&) noexcept
    {
    }
};

struct ThrowingPointerListener final {
    void operator()(Tina::UI::UIRoutedPointerEvent&)
    {
    }
};

struct OversizedPointerListener final {
    std::byte storage[Tina::UI::UIRoutedPointerCallback::InlineStorageBytes + 1]{};

    void operator()(Tina::UI::UIRoutedPointerEvent&) noexcept
    {
    }
};

[[maybe_unused]] void instantiateFixedInlineCallbackOperations()
{
    auto event = Tina::UI::Detail::UIRoutedPointerEventAccess::Create({});
    (void)event.claimPointerButton(Tina::Platform::PointerButton::Primary);
    (void)event.claimPointerButton(
        static_cast<Tina::Platform::PointerButton>(
            Tina::Platform::PointerButtonCount));
    Tina::UI::UIRoutedPointerCallback callback{NoexceptPointerListener{}};
    callback(event);
    Tina::UI::UIRoutedPointerCallback moved{std::move(callback)};
    moved.reset();
}

} // namespace

static_assert(std::is_enum_v<Tina::UI::UIRoutedPointerEventKind>);
static_assert(std::is_enum_v<Tina::UI::UIEventPhase>);
static_assert(std::is_enum_v<Tina::UI::UIEventPhaseMask>);
static_assert(std::is_same_v<
              std::underlying_type_t<Tina::UI::UIEventPhaseMask>,
              Tina::u8>);
static_assert(Tina::UI::hasEventPhase(
    Tina::UI::UIEventPhaseMask::Capture | Tina::UI::UIEventPhaseMask::Bubble,
    Tina::UI::UIEventPhaseMask::Capture));
static_assert(Tina::UI::UIEventPhaseMask::All
    == (Tina::UI::UIEventPhaseMask::Capture
        | Tina::UI::UIEventPhaseMask::Target
        | Tina::UI::UIEventPhaseMask::Bubble));

static_assert(std::is_trivially_copyable_v<Tina::UI::UIPointerInputEvent>);
static_assert(!std::is_copy_constructible_v<Tina::UI::UIRoutedPointerEvent>);
static_assert(!std::is_move_constructible_v<Tina::UI::UIRoutedPointerEvent>);
static_assert(std::is_trivially_copyable_v<Tina::UI::UIRoutedPointerListenerDesc>);
static_assert(std::is_trivially_copyable_v<Tina::UI::UIPointerRouteResult>);

static_assert(Tina::UI::UIRoutedPointerCallback::InlineStorageBytes == 48);
static_assert(!std::is_copy_constructible_v<Tina::UI::UIRoutedPointerCallback>);
static_assert(!std::is_copy_assignable_v<Tina::UI::UIRoutedPointerCallback>);
static_assert(std::is_nothrow_move_constructible_v<Tina::UI::UIRoutedPointerCallback>);
static_assert(std::is_nothrow_move_assignable_v<Tina::UI::UIRoutedPointerCallback>);
static_assert(std::is_nothrow_destructible_v<Tina::UI::UIRoutedPointerCallback>);
static_assert(std::is_constructible_v<
              Tina::UI::UIRoutedPointerCallback,
              NoexceptPointerListener>);
static_assert(!std::is_constructible_v<
              Tina::UI::UIRoutedPointerCallback,
              ThrowingPointerListener>);
static_assert(!std::is_constructible_v<
              Tina::UI::UIRoutedPointerCallback,
              OversizedPointerListener>);
