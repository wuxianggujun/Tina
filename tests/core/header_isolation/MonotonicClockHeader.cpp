#include <tina/core/time/MonotonicClock.hpp>

#include <type_traits>

static_assert(std::is_abstract_v<Tina::Core::IMonotonicClock>);
