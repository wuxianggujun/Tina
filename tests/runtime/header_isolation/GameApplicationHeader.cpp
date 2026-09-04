#include <tina/runtime/GameApplication.hpp>

#include <type_traits>
#include <utility>

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

static_assert(std::is_same_v<
    decltype(std::declval<const Tina::GameStateEnterContext&>().renderDevice()),
    Tina::Render::IRenderDevice&>);
static_assert(std::is_same_v<
    decltype(std::declval<const Tina::GameStateExitContext&>().renderDevice()),
    Tina::Render::IRenderDevice&>);
static_assert(std::is_same_v<
    decltype(std::declval<const Tina::FrameUpdateContext&>().renderDevice()),
    Tina::Render::IRenderDevice*>);
static_assert(noexcept(std::declval<const Tina::GameStateEnterContext&>().renderDevice()));
static_assert(noexcept(std::declval<const Tina::GameStateExitContext&>().renderDevice()));
static_assert(noexcept(std::declval<const Tina::FrameUpdateContext&>().renderDevice()));
