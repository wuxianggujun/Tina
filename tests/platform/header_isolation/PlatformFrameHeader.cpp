#include <tina/platform/PlatformFrame.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Platform::PlatformFrameBuilder>);
static_assert(Tina::Platform::PlatformFrameCapacityConfig::MaximumInputTransitionCapacity == 4096);
static_assert(Tina::Platform::PlatformFrameCapacityConfig::DefaultInputTextByteCapacity == 16384);
static_assert(Tina::Platform::PlatformFrameCapacityConfig::MaximumInputTextByteCapacity == 1048576);
static_assert(Tina::Platform::FrameBatchAppendResult::InvalidPayload !=
              Tina::Platform::FrameBatchAppendResult::Appended);
static_assert(Tina::Platform::PlatformFrameBuilder::MaximumGamepadSlots == 16);
static_assert(Tina::Platform::PlatformFrameBuilder::MaximumGamepads ==
              Tina::Platform::PlatformFrameBuilder::MaximumGamepadSlots);
