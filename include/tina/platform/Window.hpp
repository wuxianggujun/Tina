#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/id/GenerationId.hpp>

#include <compare>
#include <string>

namespace Tina::Platform {

struct WindowRegistryTag;
using WindowId = Core::GenerationId<WindowRegistryTag>;

struct LogicalExtent final {
    u32 width = 0;
    u32 height = 0;

    auto operator<=>(const LogicalExtent&) const = default;
};

struct FramebufferExtent final {
    u32 width = 0;
    u32 height = 0;

    auto operator<=>(const FramebufferExtent&) const = default;
};

struct ContentScale final {
    float x = 1.0F;
    float y = 1.0F;

    auto operator<=>(const ContentScale&) const = default;
};

// One authoritative, atomically committed set of facts about a window.
// A WindowInputSnapshot refers back to this revision instead of duplicating
// focus, visibility, size, or scale state.
struct WindowMetricsSnapshot final {
    WindowId window{};
    LogicalExtent logicalExtent{};
    FramebufferExtent framebufferExtent{};
    ContentScale contentScale{};
    u64 revision = 0;
    bool focused = false;
    bool minimized = false;
    bool visible = false;
};

enum class WindowMode : u8 {
    Windowed,
    BorderlessFullscreen,
};

// How the primary window treats the mouse cursor.
//
// Free is the only mode a UI or a 2D world pick can use: the cursor is visible and
// its position is a real point inside the window, so hit testing means something.
//
// Locked hides the cursor and stops it from leaving the window, which turns pointer
// position into an unbounded virtual value whose per-move delta is the only useful
// part. A first-person camera requires it: under Free the cursor reaches the edge of
// the screen and stops moving, so the delta becomes zero and the camera jams while
// the player is still moving the mouse.
enum class PointerCaptureMode : u8 {
    Free,
    Locked,
};

struct PrimaryWindowConfig final {
    std::string title = "Tina";
    LogicalExtent initialLogicalExtent{1280, 720};
    WindowMode mode = WindowMode::Windowed;
    bool resizable = true;
    bool initiallyVisible = true;
    // Applied when the window is created, so a first-person game does not show one
    // frame with a free cursor before it locks.
    PointerCaptureMode pointerCapture = PointerCaptureMode::Free;
};

} // namespace Tina::Platform
