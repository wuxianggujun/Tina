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

struct PrimaryWindowConfig final {
    std::string title = "Tina";
    LogicalExtent initialLogicalExtent{1280, 720};
    WindowMode mode = WindowMode::Windowed;
    bool resizable = true;
    bool initiallyVisible = true;
};

} // namespace Tina::Platform
