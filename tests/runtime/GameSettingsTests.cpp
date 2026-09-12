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

TEST(GameSettingsTest, RejectsOldUnknownAndRepeatedSchemaVersions)
{
    EXPECT_FALSE(parseGameSettingsText("version=1\nvsync=0\n"));
    EXPECT_FALSE(parseGameSettingsText("version=999999\nvsync=0\n"));
    EXPECT_FALSE(parseGameSettingsText("version=2\nversion=2\n"));
}

// An unknown key is tolerated so a downgrade does not destroy what a newer build
// wrote, but a malformed value for a key we *do* understand is reported instead of
// silently discarding a deliberate choice.
TEST(GameSettingsTest, IgnoresUnknownKeysButRejectsMalformedKnownValues)
{
    auto tolerated = parseGameSettingsText("version=2\nfuture.option=whatever\nvsync=1\n");
    ASSERT_TRUE(tolerated) << tolerated.error().message;
    EXPECT_TRUE(tolerated->vsyncEnabled);

    const std::array<std::string_view, 5> malformed{
        "version=2\nvsync=maybe\n",
        "version=2\naudio.bus0.volume=1.5\n",
        "version=2\naudio.bus9.volume=0.5\n",
        "version=2\ninput.binding0=0:1:0:key:60\n",
        "version=2\ninput.binding0=11:1:0:key:99999\n",
    };
    for (const std::string_view candidate : malformed)
    {
        auto parsed = parseGameSettingsText(candidate);
        ASSERT_FALSE(parsed) << "accepted malformed settings: " << candidate;
        EXPECT_EQ(parsed.error().code, Core::CoreErrorCode::InvalidArgument);
    }

    // Version must come first, otherwise a value could be parsed under the wrong
    // schema before the version is known.
    auto lateVersion = parseGameSettingsText("vsync=1\nversion=2\n");
    ASSERT_FALSE(lateVersion);
    auto noVersion = parseGameSettingsText("vsync=1\n");
    ASSERT_FALSE(noVersion);
}

TEST(GameSettingsTest, VolumeUsesTheCoreFiniteDecimalGrammar)
{
    const auto valid = parseGameSettingsText("version=2\naudio.bus0.volume=2.5e-1\n");
    ASSERT_TRUE(valid) << valid.error().message;
    EXPECT_FLOAT_EQ(valid->audioBuses[0].volume, 0.25F);
    for (const auto text : {
        "version=2\naudio.bus0.volume=0x1p-1\n",
        "version=2\naudio.bus0.volume=0.5garbage\n",
        "version=2\naudio.bus0.volume=nan\n",
        "version=2\naudio.bus0.volume=inf\n",
        "version=2\naudio.bus0.volume=1e400\n",
        "version=2\naudio.bus0.volume=-0.25\n",
        "version=4294967296\n",
    })
    {
        EXPECT_FALSE(parseGameSettingsText(text)) << text;
    }
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

TEST(GameSettingsTest, MergeUsesExplicitIdsAndIgnoresRemovedBindings)
{
    const std::array<InputActionBinding, 2> startup{
        InputActionBinding{
            .binding = InputBindingId{7},
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::A},
            .action = MoveAction,
            .domain = InputActionDomain::Frame,
        },
        InputActionBinding{
            .binding = InputBindingId{8},
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
        // A second explicitly identified edge.
        InputBindingSetting{
            .action = JumpAction,
            .binding = InputBindingId{8},
            .domain = InputActionDomain::Simulation,
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::K},
        },
        // An action the game no longer declares must not block startup.
        InputBindingSetting{
            .action = InputActionId{999},
            .binding = InputBindingId{999},
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

TEST(GameSettingsTest, PointerSlotsAndSharedControlsRoundTripWithoutLosingIdentity)
{
    GameSettings original;
    for (Platform::PointerId pointer = 0; pointer < Platform::PointerCapacity; ++pointer)
    {
        original.inputBindings.push_back({
            .action = MoveAction,
            .binding = InputBindingId{static_cast<u32>(pointer) + 1U},
            .input = PointerButtonBinding{.pointer = pointer},
        });
        original.inputBindings.push_back({
            .action = JumpAction,
            .binding = InputBindingId{
                static_cast<u32>(pointer) + 1U + static_cast<u32>(Platform::PointerCapacity)},
            .domain = InputActionDomain::Frame,
            .input = PointerButtonBinding{.pointer = pointer},
        });
    }
    auto text = writeGameSettingsText(original);
    ASSERT_TRUE(text) << text.error().message;
    EXPECT_NE(text->find("pointer:7:0"), std::string::npos);
    auto parsed = parseGameSettingsText(*text);
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_EQ(*parsed, original);
}

TEST(GameSettingsTest, RejectsAmbiguousOrInvalidPersistedEdges)
{
    GameSettings settings = populatedSettings();
    settings.inputBindings.front().binding = {};
    EXPECT_FALSE(writeGameSettingsText(settings));
    settings = populatedSettings();
    settings.inputBindings.back().binding = settings.inputBindings.front().binding;
    EXPECT_FALSE(writeGameSettingsText(settings));
    settings = populatedSettings();
    settings.inputBindings.front().domain = static_cast<InputActionDomain>(255);
    EXPECT_FALSE(writeGameSettingsText(settings));
    settings = populatedSettings();
    settings.inputBindings.front().input = PointerButtonBinding{.pointer = Platform::PointerCapacity};
    EXPECT_FALSE(writeGameSettingsText(settings));

    for (const auto text : {
        "version=2\ninput.binding0=11:0:0:key:1\n",
        "version=2\ninput.binding0=11:1:0:pointer:0\n",
        "version=2\ninput.binding0=11:1:0:pointer:8:0\n",
        "version=2\ninput.binding0=11:1:0:pointer:1:0\ninput.binding1=12:1:1:pointer:1:0\n",
        "version=2\ninput.binding0=11:1:0:pointer:1:0\ninput.binding1=11:2:0:pointer:1:0\n",
        "version=2\ninput.binding0=11:1:0:pointer:1:0\ninput.binding0=12:2:1:pointer:1:0\n",
    })
    {
        EXPECT_FALSE(parseGameSettingsText(text)) << text;
    }
}

TEST(GameSettingsTest, MergeNeverFallsBackToFirstSiblingOfTheSameAction)
{
    const std::array startup{
        InputActionBinding{.binding = InputBindingId{1}, .input = PrimaryWindowKeyBinding{Platform::Key::A}, .action = MoveAction},
        InputActionBinding{.binding = InputBindingId{2}, .input = PrimaryWindowKeyBinding{Platform::Key::B}, .action = MoveAction},
    };
    const std::array persisted{
        InputBindingSetting{.action = MoveAction, .binding = InputBindingId{2}, .input = PrimaryWindowKeyBinding{Platform::Key::C}},
        InputBindingSetting{.action = MoveAction, .binding = InputBindingId{99}, .input = PrimaryWindowKeyBinding{Platform::Key::D}},
    };
    auto merged = mergeInputBindingSettings(startup, persisted);
    ASSERT_TRUE(merged) << merged.error().message;
    EXPECT_EQ((*merged)[0].input, startup[0].input);
    EXPECT_EQ((*merged)[1].input, persisted[0].input);
    EXPECT_EQ(startup[1].input, ActionBindingPattern{PrimaryWindowKeyBinding{Platform::Key::B}});
}

TEST(GameSettingsTest, MergeValidatesIdentityAndTheWholeProspectiveGraph)
{
    const std::array startup{
        InputActionBinding{.binding = InputBindingId{1}, .input = PrimaryWindowKeyBinding{Platform::Key::A}, .action = MoveAction},
        InputActionBinding{.binding = InputBindingId{2}, .input = PrimaryWindowKeyBinding{Platform::Key::B}, .action = MoveAction},
    };
    std::array persisted{
        InputBindingSetting{.action = JumpAction, .binding = InputBindingId{1}, .input = PrimaryWindowKeyBinding{Platform::Key::C}},
    };
    EXPECT_FALSE(mergeInputBindingSettings(startup, persisted));
    persisted[0].action = MoveAction;
    persisted[0].domain = InputActionDomain::Frame;
    EXPECT_FALSE(mergeInputBindingSettings(startup, persisted));
    persisted[0].domain = InputActionDomain::Simulation;
    persisted[0].input = startup[1].input;
    EXPECT_FALSE(mergeInputBindingSettings(startup, persisted));

    const std::array swapped{
        InputBindingSetting{.action = MoveAction, .binding = InputBindingId{1}, .input = startup[1].input},
        InputBindingSetting{.action = MoveAction, .binding = InputBindingId{2}, .input = startup[0].input},
    };
    auto result = mergeInputBindingSettings(startup, swapped);
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ((*result)[0].input, startup[1].input);
    EXPECT_EQ((*result)[1].input, startup[0].input);
}

TEST(GameSettingsTest, OversizedTextAndBindingTablesFailBeforeUnboundedAllocation)
{
    EXPECT_FALSE(parseGameSettingsText(std::string(GameSettingsWire::MaxFileBytes + 1U, ' ')));
    GameSettings settings;
    settings.inputBindings.resize(InputActionMapCapacityConfig::MaximumActionBindingCapacity + 1U);
    EXPECT_FALSE(writeGameSettingsText(settings));
}

} // namespace
} // namespace Tina
