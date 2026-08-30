#include <tina/ui/UIFlow.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace {

[[maybe_unused]] Tina::UI::UIFlowLayerId layer{};
[[maybe_unused]] Tina::UI::UIFlowScreenId screen{};

struct NoexceptFlowAction final {
    void operator()(const Tina::UI::UIFlowActionEvent&) noexcept
    {
    }
};

struct ThrowingFlowAction final {
    void operator()(const Tina::UI::UIFlowActionEvent&)
    {
    }
};

struct MutableFlowAction final {
    void operator()(Tina::UI::UIFlowActionEvent&) noexcept
    {
    }
};

struct OversizedFlowAction final {
    std::byte storage[Tina::UI::UIFlowActionCallback::InlineStorageBytes + 1]{};

    void operator()(const Tina::UI::UIFlowActionEvent&) noexcept
    {
    }
};

[[maybe_unused]] void instantiateFixedInlineCallbackOperations()
{
    Tina::UI::UIFlowActionEvent event{
        .action = Tina::UI::UIFlowAction::Back,
        .source = Tina::UI::UIFlowActionSource::Keyboard,
        .platformFrame = Tina::Platform::PlatformFrameId{1},
        .sourceSequence = 7,
    };

    Tina::UI::UIFlowActionCallback callback{NoexceptFlowAction{}};
    callback(event);
    Tina::UI::UIFlowActionCallback moved{std::move(callback)};
    moved.reset();
}

} // namespace

static_assert(std::is_trivially_copyable_v<Tina::UI::UIFlowActionEvent>);
static_assert(std::is_nothrow_destructible_v<Tina::UI::UIFlowActionEvent>);

static_assert(Tina::UI::UIFlowActionCallback::InlineStorageBytes == 48);
static_assert(!std::is_copy_constructible_v<Tina::UI::UIFlowActionCallback>);
static_assert(!std::is_copy_assignable_v<Tina::UI::UIFlowActionCallback>);
static_assert(std::is_nothrow_move_constructible_v<Tina::UI::UIFlowActionCallback>);
static_assert(std::is_nothrow_move_assignable_v<Tina::UI::UIFlowActionCallback>);
static_assert(std::is_nothrow_destructible_v<Tina::UI::UIFlowActionCallback>);
static_assert(std::is_constructible_v<
              Tina::UI::UIFlowActionCallback,
              NoexceptFlowAction>);
static_assert(!std::is_constructible_v<
              Tina::UI::UIFlowActionCallback,
              ThrowingFlowAction>);
static_assert(!std::is_constructible_v<
              Tina::UI::UIFlowActionCallback,
              MutableFlowAction>);
static_assert(!std::is_constructible_v<
              Tina::UI::UIFlowActionCallback,
              OversizedFlowAction>);
// The storing constructor must never be a candidate for this type itself. Asking the
// question at all is what used to re-enter the constructor's own constraint under Clang.
static_assert(!std::is_constructible_v<
              Tina::UI::UIFlowActionCallback,
              Tina::UI::UIFlowActionCallback&>);
