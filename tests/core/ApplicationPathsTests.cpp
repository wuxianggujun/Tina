#include <tina/core/io/ApplicationPaths.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace Tina::Core {
namespace {

[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text)
{
    std::u8string bytes;
    bytes.reserve(text.size());
    for (const char byte : text)
    {
        bytes.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
    }
    return std::filesystem::path{std::move(bytes)};
}

// The directory holding this test executable is ground truth the test can check
// against the filesystem, which is what makes this more than a self-consistency
// assertion.
TEST(ApplicationPathsTest, ResolvesTheDirectoryHoldingTheRunningExecutable)
{
    const auto directory = applicationDirectory();
    ASSERT_TRUE(directory.has_value()) << directory.error().message;
    EXPECT_FALSE(directory->empty());

    const std::filesystem::path resolved = pathFromUtf8(*directory);
    EXPECT_TRUE(resolved.is_absolute());
    EXPECT_TRUE(std::filesystem::exists(resolved));
    EXPECT_TRUE(std::filesystem::is_directory(resolved));
}

// A test executable never sits at a filesystem root, so the only separator-
// terminated result the implementation can produce is excluded here.
TEST(ApplicationPathsTest, LeavesNoTrailingSeparator)
{
    const auto directory = applicationDirectory();
    ASSERT_TRUE(directory.has_value()) << directory.error().message;
    ASSERT_FALSE(directory->empty());
    EXPECT_NE(directory->back(), '/');
    EXPECT_NE(directory->back(), '\\');
}

TEST(ApplicationPathsTest, RepeatedResolutionAgrees)
{
    const auto first = applicationDirectory();
    const auto second = applicationDirectory();
    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_TRUE(second.has_value()) << second.error().message;
    EXPECT_EQ(*first, *second);
}

TEST(ApplicationPathsTest, JoinsNestedRelativePathsBelowTheExecutable)
{
    const auto directory = applicationDirectory();
    ASSERT_TRUE(directory.has_value()) << directory.error().message;

    const auto file = applicationFilePath("assets/tilemap/level.tinapack");
    ASSERT_TRUE(file.has_value()) << file.error().message;
    EXPECT_EQ(*file, *directory + "/assets/tilemap/level.tinapack");
}

TEST(ApplicationPathsTest, JoinsASingleComponent)
{
    const auto directory = applicationDirectory();
    ASSERT_TRUE(directory.has_value()) << directory.error().message;

    const auto file = applicationFilePath("catalog.tinacatalog");
    ASSERT_TRUE(file.has_value()) << file.error().message;
    EXPECT_EQ(*file, *directory + "/catalog.tinacatalog");
}

// Composing a path must not depend on the target existing: a caller reports a
// missing asset from the read that fails, not from the resolve, and a read-only
// or empty install must still be able to name what it was looking for.
TEST(ApplicationPathsTest, ComposesPathsThatDoNotExist)
{
    const auto file = applicationFilePath("definitely/not/present.tinapack");
    ASSERT_TRUE(file.has_value()) << file.error().message;
    EXPECT_FALSE(std::filesystem::exists(pathFromUtf8(*file)));
}

TEST(ApplicationPathsTest, RejectsRelativePathsThatCouldEscapeTheExecutableDirectory)
{
    const std::array<std::string_view, 12> rejected{
        "",                  // names nothing
        "/etc/passwd",       // absolute
        "..",                // parent
        "../sibling",        // parent
        "assets/../..",      // parent after a valid component
        "./assets",          // no-op component
        "assets/./pack",     // no-op component
        "assets//pack",      // empty component
        "assets/",           // empty trailing component
        "assets\\pack",      // '\' is not the contract separator
        "C:/assets",         // drive-qualified
        "assets/\xFFpack",   // not UTF-8
    };
    for (const std::string_view candidate : rejected)
    {
        const auto file = applicationFilePath(candidate);
        ASSERT_FALSE(file.has_value()) << "accepted: " << candidate;
        EXPECT_EQ(file.error().code, CoreErrorCode::InvalidArgument) << "for: " << candidate;
    }
}

TEST(ApplicationPathsTest, RejectsRelativePathsContainingNul)
{
    const auto file = applicationFilePath(std::string_view{"assets\0pack", 11});
    ASSERT_FALSE(file.has_value());
    EXPECT_EQ(file.error().code, CoreErrorCode::InvalidArgument);
}

} // namespace
} // namespace Tina::Core
