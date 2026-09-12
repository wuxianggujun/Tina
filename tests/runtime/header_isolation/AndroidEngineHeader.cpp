#include <tina/android/AndroidEngine.hpp>

#include <type_traits>

static_assert(std::is_move_constructible_v<Tina::Android::EngineInstance>);
static_assert(!std::is_copy_constructible_v<Tina::Android::EngineInstance>);
static_assert(std::is_same_v<decltype(Tina::Android::CreateEngine(
    Tina::EngineConfig{}, Tina::Android::CreateEngineOptions{})),
    Tina::Core::Result<Tina::Android::EngineInstance>>);
