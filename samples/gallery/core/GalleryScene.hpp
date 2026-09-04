#pragma once

// The sample gallery: one menu, many scenes, one implementation for desktop and Android.
//
// This exists because the other twenty samples cannot answer "show me the engine on a phone". They are
// gate programs -- they run a fixed frame count, print evidence and return an exit code -- and eight of
// them link the GLFW desktop engine, which does not build for Android at all. Neither shape survives
// being handed to a person with a touchscreen.
//
// So the gallery is additive: it is a new front-end over the same engine, and the gate samples stay
// exactly as they are. They are CI evidence, and rewriting them into interactive scenes would trade
// something proven for something merely nicer to look at.
//
// A scene is written once. Both hosts compose the engine themselves -- desktop through the GLFW
// composition root, Android through the existing JNI bridge -- and then hand it the same
// createGalleryApplication(). That is the whole reason the scene interface below names no platform type.

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>

#include <memory>
#include <span>
#include <string_view>

namespace Tina::Gallery {

// A scene the gallery can push, described without constructing it.
//
// The factory is deferred rather than the scene being built up front because a scene owns UI nodes and
// engine resources: building all of them at startup would pay for every scene to show one, and the menu
// would hold roots it never displays.
struct GalleryEntry final {
    // Shown in the menu. Kept short enough to fit a phone's width at the menu's text size.
    std::string_view title{};
    // One line on what the scene demonstrates, shown under the title.
    std::string_view summary{};
    // Builds the scene. Returning a Result rather than a bare pointer keeps a scene's own setup failure
    // (a missing font, an exhausted node budget) reportable instead of surfacing as a null deref.
    Core::Result<std::unique_ptr<IGameState>> (*create)();
};

// Every scene the gallery offers, in menu order.
//
// A free function rather than a table the host passes in: the list is the same on every platform, and
// letting each host supply its own would let desktop and Android silently drift into different galleries
// -- which is exactly the "samples are scattered" problem this is meant to fix.
[[nodiscard]] std::span<const GalleryEntry> galleryEntries() noexcept;

// The gallery application. Its initial state is the menu.
//
// Scenes return to the menu through FrameUpdateContext::requestPop(), so the menu stays on the stack
// underneath rather than being rebuilt on every return. A rebuilt menu would lose scroll position and
// re-run every scene's layout budget on the way back.
[[nodiscard]] std::unique_ptr<IGameApplication> createGalleryApplication() noexcept;

} // namespace Tina::Gallery
