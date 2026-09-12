#pragma once

#include <tina/audio/AudioTypes.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/runtime/InputActionMap.hpp>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Tina {

namespace GameSettingsWire {
// Version 2 preserves pointer identity and requires stable binding ids. Older
// schemas fail closed; the product decides how to surface or reset that file.
inline constexpr Core::u32 SchemaVersion = 2;
// Ample for the fixed audio buses, display flags and a full rebinding table,
// while still bounding what a corrupt or hostile file can allocate.
inline constexpr Core::u64 MaxFileBytes = 256U * 1024U;
} // namespace GameSettingsWire

struct AudioBusSetting final {
    float volume = 1.0F;
    bool muted = false;

    friend constexpr bool operator==(const AudioBusSetting&, const AudioBusSetting&) noexcept = default;
};

// One persisted edge, keyed only by its explicit non-zero binding id. Action and
// domain verify its identity; they are never a first-match fallback. Products
// must assign stable ids in their startup configuration to persist rebindings.
struct InputBindingSetting final {
    InputActionId action{};
    InputBindingId binding{};
    InputActionDomain domain = InputActionDomain::Simulation;
    ActionBindingPattern input{};

    friend bool operator==(const InputBindingSetting&, const InputBindingSetting&) = default;
};

// Player-facing settings a product may persist. This is deliberately a plain
// value type: Runtime owns no settings state, so a product decides when to load,
// when to apply and when to save.
struct GameSettings final {
    std::array<AudioBusSetting, Audio::AudioBusCount> audioBuses{};
    bool vsyncEnabled = true;
    // Persisted rebindings, applied over whatever the startup config declared.
    std::vector<InputBindingSetting> inputBindings{};

    friend bool operator==(const GameSettings&, const GameSettings&) = default;
};

// Serializes to a line-oriented UTF-8 `key=value` text form. Text rather than a
// cooked binary payload because this file is user-editable by nature and survives
// version skew better; the format is not an asset and never enters the Catalog.
[[nodiscard]] Core::Result<std::string> writeGameSettingsText(const GameSettings& settings);

// Parses version 2 only. Unknown keys within this schema are ignored, but a
// malformed value for a *known* key is an
// error rather than a silent default, because silently discarding a player's
// deliberate choice is worse than telling the product the file is broken.
[[nodiscard]] Core::Result<GameSettings> parseGameSettingsText(std::string_view text);

// Reads and parses utf8Path. A missing file yields defaults with `loaded` false,
// which is the first-run case and not an error. A present but unreadable or
// malformed file fails, so a product can warn instead of silently resetting.
struct GameSettingsLoadResult final {
    GameSettings settings{};
    bool loaded = false;
};

[[nodiscard]] Core::Result<GameSettingsLoadResult> loadGameSettingsFromFile(std::string_view utf8Path);

// Writes atomically through Core::writeFile, creating missing parents, so an
// interrupted save cannot leave a truncated settings file behind.
[[nodiscard]] Core::Status saveGameSettingsToFile(std::string_view utf8Path, const GameSettings& settings);

// Transactionally merges by explicit binding id and validates the resulting
// graph. An id whose action/domain changed is an error; removed ids are ignored.
// Shared controls are valid, duplicate Action/pattern edges are not. The caller
// still validates EngineConfig to enforce its configured capacity limits.
[[nodiscard]] Core::Result<std::vector<InputActionBinding>>
mergeInputBindingSettings(std::span<const InputActionBinding> startupBindings,
                          std::span<const InputBindingSetting> persisted);

} // namespace Tina
