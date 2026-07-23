#include <tina/runtime/GameStateCommands.hpp>

namespace {
[[maybe_unused]] constexpr Tina::usize kMax = Tina::GameStatePendingCommands::MaxStackDepth;
[[maybe_unused]] Tina::GameStateStack kStack{};
} // namespace
