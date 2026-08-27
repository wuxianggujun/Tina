#include <tina/runtime/GameSettings.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>
#include <system_error>

namespace Tina {
namespace {

constexpr InputActionId MoveAction{11};
constexpr InputActionId JumpAction{12};

[[nodiscard]] GameSettings populatedSettings()
{
    GameSettings settings{};
    settings.vsyncEnabled = false;
    settings.audioBuses[static_cast<Core::usize>(Audio::AudioBusId::Master)] = {.volume = 0.75F, .muted = false};
    settings.audioBuses[static_cast<Core::usize>(Audio::AudioBusId::Music)] = {.volume = 0.5F, .muted = true};
    settings.audioBuses[static_cast<Core::usize>(Audio::AudioBusId::Sfx)] = {.volume = 0.25F, .muted = false};
    settings.inputBindings.push_back(InputBindingSetting{
        .action = MoveAction,
        .binding = InputBindingId{7},
        .domain = InputActionDomain::Frame,
        .input = PrimaryWindowKeyBinding{.key = Platform::Key::J},
    });
    settings.inputBindings.push_back(InputBindingSetting{
        .action = JumpAction,
        .binding = InputBindingId{8},
        .domain = InputActionDomain::Simulation,
        .input = StandardGamepadAxisBinding{
            .axis = Platform::GamepadAxis::RightY,
            .valueMode = GamepadAxisValueMode::Trigger,
        },
    });
    return settings;
}

TEST(GameSettingsTest, TextRoundTripPreservesEveryField)
{
    const GameSettings original = populatedSettings();
    auto text = writeGameSettingsText(original);
    ASSERT_TRUE(text) << text.error().message;

    auto parsed = parseGameSettingsText(*text);
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_EQ(*parsed, original);
}

// A different schema means an upgrade or downgrade, not corruption, so the player
// gets a working game with defaults rather than a startup error.
TEST(GameSettingsTest, UnknownSchemaVersionYieldsDefaults)
{
    auto parsed = parseGameSettingsText("version=999999\nvsync=0\n");
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_EQ(*parsed, GameSettings{});
}

// An unknown key is tolerated so a downgrade does not destroy what a newer build
// wrote, but a malformed value for a key we *do* understand is reported instead of
// silently discarding a deliberate choice.
TEST(GameSettingsTest, IgnoresUnknownKeysButRejectsMalformedKnownValues)
{
    auto tolerated = parseGameSettingsText("version=1\nfuture.option=whatever\nvsync=1\n");
    ASSERT_TRUE(tolerated) << tolerated.error().message;
    EXPECT_TRUE(tolerated->vsyncEnabled);

    const std::array<std::string_view, 5> malformed{
        "version=1\nvsync=maybe\n",
        "version=1\naudio.bus0.volume=1.5\n",
        "version=1\naudio.bus9.volume=0.5\n",
        "version=1\ninput.binding0=0:1:0:key:60\n",
        "version=1\ninput.binding0=11:1:0:key:99999\n",
    };
    for (const std::string_view candidate : malformed)
    {
        auto parsed = parseGameSettingsText(candidate);
        ASSERT_FALSE(parsed) << "accepted malformed settings: " << candidate;
        EXPECT_EQ(parsed.error().code, Core::CoreErrorCode::InvalidArgument);
    }

    // Version must come first, otherwise a value could be parsed under the wrong
    // schema before the version is known.
    auto lateVersion = parseGameSettingsText("vsync=1\nversion=1\n");
    ASSERT_FALSE(lateVersion);
    auto noVersion = parseGameSettingsText("vsync=1\n");
    ASSERT_FALSE(noVersion);
}

TEST(GameSettingsTest, MissingFileIsFirstRunAndSaveRoundTrips)
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "tina_game_settings_test";
    const std::filesystem::path path = directory / "settings.txt";
    std::error_code removeError;
    std::filesystem::remove_all(directory, removeError);

    auto missing = loadGameSettingsFromFile(path.string());
    ASSERT_TRUE(missing) << missing.error().message;
    EXPECT_FALSE(missing->loaded);
    EXPECT_EQ(missing->settings, GameSettings{});

    const GameSettings original = populatedSettings();
    // The parent directory does not exist yet; the save must create it.
    ASSERT_TRUE(saveGameSettingsToFile(path.string(), original));
    auto loaded = loadGameSettingsFromFile(path.string());
    ASSERT_TRUE(loaded) << loaded.error().message;
    EXPECT_TRUE(loaded->loaded);
    EXPECT_EQ(loaded->settings, original);

    std::filesystem::remove_all(directory, removeError);
}

// Automatic binding ids are assigned in config order at startup, so they are not
// stable across runs. A persisted entry keys off the explicit id first and falls
// back to (action, domain).
TEST(GameSettingsTest, MergeReplacesByExplicitIdThenActionAndIgnoresStaleEntries)
{
    const std::array<InputActionBinding, 2> startup{
        InputActionBinding{
            .binding = InputBindingId{7},
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::A},
            .action = MoveAction,
            .domain = InputActionDomain::Frame,
        },
        InputActionBinding{
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::B},
            .action = JumpAction,
            .domain = InputActionDomain::Simulation,
        },
    };
    const std::array<InputBindingSetting, 3> persisted{
        // Matches by explicit id.
        InputBindingSetting{
            .action = MoveAction,
            .binding = InputBindingId{7},
            .domain = InputActionDomain::Frame,
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::J},
        },
        // No explicit id, so it must match on (action, domain).
        InputBindingSetting{
            .action = JumpAction,
            .domain = InputActionDomain::Simulation,
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::K},
        },
        // An action the game no longer declares must not block startup.
        InputBindingSetting{
            .action = InputActionId{999},
            .domain = InputActionDomain::Frame,
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::L},
        },
    };

    auto merged = mergeInputBindingSettings(startup, persisted);
    ASSERT_TRUE(merged) << merged.error().message;
    ASSERT_EQ(merged->size(), startup.size());
    EXPECT_EQ(std::get<PrimaryWindowKeyBinding>((*merged)[0].input).key, Platform::Key::J);
    EXPECT_EQ(std::get<PrimaryWindowKeyBinding>((*merged)[1].input).key, Platform::Key::K);
    // Everything other than the pattern is preserved.
    EXPECT_EQ((*merged)[0].action, MoveAction);
    EXPECT_EQ((*merged)[0].binding, InputBindingId{7});
    EXPECT_EQ((*merged)[1].domain, InputActionDomain::Simulation);
}

} // namespace
} // namespace Tina
