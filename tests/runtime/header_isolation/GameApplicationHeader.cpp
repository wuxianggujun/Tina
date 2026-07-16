#include <tina/runtime/GameApplication.hpp>

#include <type_traits>

static_assert(std::has_virtual_destructor_v<Tina::IGameApplication>);
static_assert(std::has_virtual_destructor_v<Tina::IGameState>);
static_assert(!std::is_copy_constructible_v<Tina::GameStartupContext>);
static_assert(!std::is_move_constructible_v<Tina::GameStartupContext>);
static_assert(!std::is_copy_constructible_v<Tina::GameStateEnterContext>);
static_assert(!std::is_move_constructible_v<Tina::GameStateEnterContext>);
static_assert(!std::is_copy_constructible_v<Tina::FixedUpdateContext>);
static_assert(!std::is_move_constructible_v<Tina::FixedUpdateContext>);
static_assert(!std::is_copy_constructible_v<Tina::FrameUpdateContext>);
static_assert(!std::is_move_constructible_v<Tina::FrameUpdateContext>);
static_assert(!std::is_copy_constructible_v<Tina::RenderSceneExtractionContext>);
static_assert(!std::is_move_constructible_v<Tina::RenderSceneExtractionContext>);
static_assert(!std::is_copy_constructible_v<Tina::UIUpdateContext>);
static_assert(!std::is_move_constructible_v<Tina::UIUpdateContext>);
