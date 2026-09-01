#pragma once

#include <tina/core/error/Result.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Tina::Desktop {

// Where a resolved UI font came from. Reported so a product or gate can state the
// provenance of the glyphs it rendered rather than inferring it from a path.
enum class UiFontSource {
    None = 0,
    Environment = 1,
    BesideExecutable = 2,
};

struct UiFontFile final {
    // Null when nothing was found. That is a normal outcome, not an error: the UI
    // falls back to placeholder text, so a product without a font still runs.
    std::shared_ptr<std::vector<std::byte>> bytes{};
    std::string path{};
    UiFontSource source = UiFontSource::None;
};

// Default location a product's font is looked for, relative to the executable.
inline constexpr const char* DefaultUiFontRelativePath = "assets/ui-font.otf";

// Finds a UI font for CreateEngineOptions::uiFontBytes. Opt-in: the engine itself
// resolves nothing, so a product that wants a different layout can ignore this and
// load its own bytes.
//
// Order is TINA_UI_FONT_PATH, then relativePath below the executable directory.
// The environment wins so a development or gate run can substitute a font without
// a rebuild -- which is what the compiled-in path used to be doing, minus the part
// where the path survived into shipped binaries.
//
// A missing file is not an error and leaves bytes null; only an unreadable file
// that exists is reported as one, since silently rendering placeholder text would
// hide a corrupt or truncated install.
[[nodiscard]] Core::Result<UiFontFile> resolveUiFontBytes(
    const char* relativePath = DefaultUiFontRelativePath);

} // namespace Tina::Desktop
