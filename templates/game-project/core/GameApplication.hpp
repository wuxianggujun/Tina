#pragma once

#include <tina/runtime/GameApplication.hpp>

#include <memory>

namespace MyGame {

// The one seam between portable content and a platform frontend.
//
// It takes no arguments on purpose. Everything the game needs to find its files arrives
// through EngineConfig::contentRoot, which the frontend fills in before creating the engine
// and which every phase context hands back. That is what keeps this header free of paths: a
// path is the one thing a browser, an APK and a desktop install genuinely disagree about.
[[nodiscard]] std::unique_ptr<Tina::IGameApplication> createApplication() noexcept;

} // namespace MyGame
