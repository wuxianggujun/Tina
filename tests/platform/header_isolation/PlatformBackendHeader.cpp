#include <tina/platform/Input.hpp>
#include <tina/platform/PlatformBackend.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/Window.hpp>

#include <optional>
#include <type_traits>
#include <utility>

static_assert(std::has_virtual_destructor_v<Tina::Platform::IPlatformBackend>);
static_assert(std::is_same_v<decltype(std::declval<Tina::Platform::IPlatformBackend&>().initialPrimaryWindowMetrics()),
                             Tina::Core::Result<std::optional<Tina::Platform::WindowMetricsSnapshot>>>);
static_assert(!Tina::Platform::WindowId{}.hasValue());
static_assert(Tina::Platform::PlatformFrameCapacityConfig::DefaultInputTransitionCapacity == 256);
static_assert(Tina::Platform::PlatformFrameCapacityConfig::DefaultInputTextByteCapacity == 16384);
static_assert(Tina::Platform::PlatformFrameCapacityConfig::DefaultPlatformEventCapacity == 64);
