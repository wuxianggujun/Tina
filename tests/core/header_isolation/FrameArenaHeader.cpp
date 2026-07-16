#include <tina/core/memory/FrameArena.hpp>

static_assert(Tina::Core::FrameArenaConfig{}.maximumAlignment == 64U);
