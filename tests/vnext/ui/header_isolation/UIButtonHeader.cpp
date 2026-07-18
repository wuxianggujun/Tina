#include <tina/ui/UIButton.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace {

struct NoexceptButtonAction final {
    void operator()(const Tina::UI::UIButtonActionEvent&) noexcept
    {
    }
};

struct ThrowingButtonAction final {
    void operator()(const Tina::UI::UIButtonActionEvent&)
    {
    }
};

struct MutableButtonAction final {
    void operator()(Tina::UI::UIButtonActionEvent&) noexcept
    {
    }
};

struct OversizedButtonAction final {
    std::byte storage[Tina::UI::UIButtonActionCallback::InlineStorageBytes + 1]{};

    void operator()(const Tina::UI::UIButtonActionEvent&) noexcept
    {
    }
};

[[maybe_unused]] void instantiateFixedInlineCallbackOperations()
{
    Tina::UI::UIButtonActionEvent event{
        .source = Tina::UI::UIButtonActivationSource::PrimaryPointer,
        .platformFrame = Tina::Platform::PlatformFrameId{1},
        .sourceSequence = 7,
    };

    Tina::UI::UIButtonActionCallback callback{NoexceptButtonAction{}};
    callback(event);
    Tina::UI::UIButtonActionCallback moved{std::move(callback)};
    moved.reset();
}

} // namespace

static_assert(std::is_enum_v<Tina::UI::UIButtonActivationSource>);
static_assert(std::is_same_v<
              std::underlying_type_t<Tina::UI::UIButtonActivationSource>,
              Tina::u8>);
static_assert(std::is_trivially_copyable_v<Tina::UI::UIButtonActionEvent>);
static_assert(std::is_nothrow_destructible_v<Tina::UI::UIButtonActionEvent>);

static_assert(Tina::UI::UIButtonActionCallback::InlineStorageBytes == 48);
static_assert(!std::is_copy_constructible_v<Tina::UI::UIButtonActionCallback>);
static_assert(!std::is_copy_assignable_v<Tina::UI::UIButtonActionCallback>);
static_assert(std::is_nothrow_move_constructible_v<Tina::UI::UIButtonActionCallback>);
static_assert(std::is_nothrow_move_assignable_v<Tina::UI::UIButtonActionCallback>);
static_assert(std::is_nothrow_destructible_v<Tina::UI::UIButtonActionCallback>);
static_assert(std::is_constructible_v<
              Tina::UI::UIButtonActionCallback,
              NoexceptButtonAction>);
static_assert(!std::is_constructible_v<
              Tina::UI::UIButtonActionCallback,
              ThrowingButtonAction>);
static_assert(!std::is_constructible_v<
              Tina::UI::UIButtonActionCallback,
              MutableButtonAction>);
static_assert(!std::is_constructible_v<
              Tina::UI::UIButtonActionCallback,
              OversizedButtonAction>);
