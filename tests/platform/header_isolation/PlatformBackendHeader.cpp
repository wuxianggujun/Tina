#include <tina/platform/Input.hpp>
#include <tina/platform/PlatformBackend.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/Window.hpp>

#include <type_traits>

static_assert(std::has_virtual_destructor_v<Tina::Platform::IPlatformBackend>);
static_assert(!Tina::Platform::WindowId{}.hasValue());
static_assert(Tina::Platform::PlatformFrameCapacityConfig::DefaultInputTransitionCapacity == 256);
static_assert(Tina::Platform::PlatformFrameCapacityConfig::DefaultInputTextByteCapacity == 16384);
static_assert(Tina::Platform::PlatformFrameCapacityConfig::DefaultPlatformEventCapacity == 64);
