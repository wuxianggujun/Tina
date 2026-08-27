#include <tina/core/io/UserPaths.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace Tina::Core {
namespace {

// Scoped environment override so a test cannot leak a base directory into the
// rest of the process.
class ScopedEnvironment final {
  public:
    ScopedEnvironment(const char* name, const char* value) : name_(name)
    {
        const char* previous = std::getenv(name);
        had_ = previous != nullptr;
        if (had_)
        {
            previous_ = previous;
        }
        apply(value);
    }

    ~ScopedEnvironment()
    {
        apply(had_ ? previous_.c_str() : nullptr);
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

  private:
    void apply(const char* value) const
    {
#if defined(_WIN32)
        // _putenv_s with an empty value removes the variable on Windows.
        static_cast<void>(_putenv_s(name_.c_str(), value == nullptr ? "" : value));
#else
        if (value == nullptr)
        {
            static_cast<void>(::unsetenv(name_.c_str()));
        } else
        {
            static_cast<void>(::setenv(name_.c_str(), value, 1));
        }
#endif
    }

    std::string name_;
    std::string previous_;
    bool had_ = false;
};

#if defined(_WIN32)
constexpr const char* ConfigBaseVariable = "LOCALAPPDATA";
constexpr const char* AbsoluteBase = "C:\\Users\\Test\\AppData\\Local";
#else
constexpr const char* ConfigBaseVariable = "XDG_CONFIG_HOME";
constexpr const char* AbsoluteBase = "/home/test/.config";
#endif

TEST(UserPathsTest, ResolvesUnderAnAbsoluteEnvironmentBase)
{
    const ScopedEnvironment base{ConfigBaseVariable, AbsoluteBase};

    auto directory = userApplicationDirectory("TinaGame");
    ASSERT_TRUE(directory) << directory.error().message;
    EXPECT_TRUE(directory->ends_with("/TinaGame"));
    EXPECT_TRUE(std::filesystem::path{*directory}.is_absolute());

    auto file = userApplicationFilePath("TinaGame", "settings.txt");
    ASSERT_TRUE(file) << file.error().message;
    EXPECT_TRUE(file->ends_with("/TinaGame/settings.txt"));
}

// A relative base would resolve against the process working directory, which is
// not a per-user location, so it must be refused rather than silently used.
TEST(UserPathsTest, RejectsRelativeEnvironmentBase)
{
    const ScopedEnvironment base{ConfigBaseVariable, "relative/path"};
#if !defined(_WIN32)
    const ScopedEnvironment home{"HOME", ""};
#else
    const ScopedEnvironment roaming{"APPDATA", ""};
#endif

    auto directory = userApplicationDirectory("TinaGame");
    ASSERT_FALSE(directory);
    EXPECT_EQ(directory.error().code, CoreErrorCode::NotFound);
}

// The segment is joined verbatim, so anything that could escape the base
// directory has to fail closed.
TEST(UserPathsTest, RejectsSegmentsThatCouldEscapeTheBase)
{
    const ScopedEnvironment base{ConfigBaseVariable, AbsoluteBase};

    const std::array<std::string_view, 7> unsafe{
        std::string_view{""},   std::string_view{"."},    std::string_view{".."},
        std::string_view{"a/b"}, std::string_view{"a\\b"}, std::string_view{"C:"},
        std::string_view{"a\0b", 3},
    };
    for (const std::string_view candidate : unsafe)
    {
        auto directory = userApplicationDirectory(candidate);
        ASSERT_FALSE(directory) << "accepted unsafe segment";
        EXPECT_EQ(directory.error().code, CoreErrorCode::InvalidArgument);
    }

    auto file = userApplicationFilePath("TinaGame", "../escape");
    ASSERT_FALSE(file);
    EXPECT_EQ(file.error().code, CoreErrorCode::InvalidArgument);
}

TEST(UserPathsTest, ConfigAndStateResolveIndependently)
{
    const ScopedEnvironment base{ConfigBaseVariable, AbsoluteBase};

    auto config = userApplicationDirectory("TinaGame", UserDirectoryKind::Config);
    auto state = userApplicationDirectory("TinaGame", UserDirectoryKind::State);
    ASSERT_TRUE(config) << config.error().message;
    ASSERT_TRUE(state) << state.error().message;
#if defined(_WIN32)
    // Windows uses one non-roaming base for both kinds.
    EXPECT_EQ(*config, *state);
#else
    // XDG separates config from state, and this test only set XDG_CONFIG_HOME.
    EXPECT_NE(*config, *state);
#endif
}

} // namespace
} // namespace Tina::Core
