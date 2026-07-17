#include <tina/runtime/spi/InputRouting.hpp>

static_assert(Tina::InputActionMapperCapacityConfig::DefaultContinuousControlClaimCapacity == 64);
static_assert(Tina::InputActionMapperCapacityConfig{}.rawInputTransitionCapacity == 256);
