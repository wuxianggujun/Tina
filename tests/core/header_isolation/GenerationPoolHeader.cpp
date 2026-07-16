#include <tina/core/id/GenerationPool.hpp>

struct GenerationPoolHeaderTag;
using HeaderPool = Tina::Core::GenerationPool<int, GenerationPoolHeaderTag>;
static_assert(std::is_move_constructible_v<HeaderPool>);
static_assert(!std::is_move_assignable_v<HeaderPool>);
