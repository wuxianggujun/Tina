#include <tina/ui/UISlider.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace {

struct NoexceptSliderChange final {
    void operator()(const Tina::UI::UISliderChangeEvent&) noexcept
    {
    }
};

struct ThrowingSliderChange final {
    void operator()(const Tina::UI::UISliderChangeEvent&)
    {
    }
};

struct MutableSliderChange final {
    void operator()(Tina::UI::UISliderChangeEvent&) noexcept
    {
    }
};

struct OversizedSliderChange final {
    std::byte storage[Tina::UI::UISliderChangeCallback::InlineStorageBytes + 1]{};

    void operator()(const Tina::UI::UISliderChangeEvent&) noexcept
    {
    }
};

[[maybe_unused]] void instantiateFixedInlineCallbackOperations()
{
    Tina::UI::UISliderChangeEvent event{
        .value = 0.5F,
        .platformFrame = Tina::Platform::PlatformFrameId{1},
        .sourceSequence = 7,
    };

    Tina::UI::UISliderChangeCallback callback{NoexceptSliderChange{}};
    callback(event);
    Tina::UI::UISliderChangeCallback moved{std::move(callback)};
    moved.reset();
}

} // namespace

static_assert(std::is_trivially_copyable_v<Tina::UI::UISliderChangeEvent>);
static_assert(std::is_nothrow_destructible_v<Tina::UI::UISliderChangeEvent>);

static_assert(Tina::UI::UISliderChangeCallback::InlineStorageBytes == 48);
static_assert(!std::is_copy_constructible_v<Tina::UI::UISliderChangeCallback>);
static_assert(!std::is_copy_assignable_v<Tina::UI::UISliderChangeCallback>);
static_assert(std::is_nothrow_move_constructible_v<Tina::UI::UISliderChangeCallback>);
static_assert(std::is_nothrow_move_assignable_v<Tina::UI::UISliderChangeCallback>);
static_assert(std::is_nothrow_destructible_v<Tina::UI::UISliderChangeCallback>);
static_assert(std::is_constructible_v<
              Tina::UI::UISliderChangeCallback,
              NoexceptSliderChange>);
static_assert(!std::is_constructible_v<
              Tina::UI::UISliderChangeCallback,
              ThrowingSliderChange>);
static_assert(!std::is_constructible_v<
              Tina::UI::UISliderChangeCallback,
              MutableSliderChange>);
static_assert(!std::is_constructible_v<
              Tina::UI::UISliderChangeCallback,
              OversizedSliderChange>);
// The storing constructor must never be a candidate for this type itself. Asking the
// question at all is what used to re-enter the constructor's own constraint under Clang.
static_assert(!std::is_constructible_v<
              Tina::UI::UISliderChangeCallback,
              Tina::UI::UISliderChangeCallback&>);
