#include "EditorSourceImportLaunchOptions.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace Detail = Tina::EditorApp::Detail;

namespace {

#if defined(_WIN32)
inline constexpr std::string_view ProjectRoot = "C:/TinaProject";
inline constexpr std::string_view RecipePath = "C:/TinaProject/Source/game.recipe";
inline constexpr std::string_view GltfPath = "C:/TinaProject/Source/hero.glb";
#else
inline constexpr std::string_view ProjectRoot = "/TinaProject";
inline constexpr std::string_view RecipePath = "/TinaProject/Source/game.recipe";
inline constexpr std::string_view GltfPath = "/TinaProject/Source/hero.glb";
#endif

[[nodiscard]] std::string argument(std::string_view name, std::string_view path)
{
    std::string result{name};
    result += path;
    return result;
}

// The parser reads from a scanner rather than a lone token, because --name value has to reach past
// the current token for its value. These cases each supply a one-token argv of their own; the
// storage has to outlive the scanner, which only holds views into it.
class OwnedArgv final {
  public:
    explicit OwnedArgv(const std::vector<std::string>& tokens)
    {
        storage_.reserve(tokens.size() + 1U);
        storage_.emplace_back("editor.exe");
        storage_.insert(storage_.end(), tokens.begin(), tokens.end());
        for (auto& token : storage_) {
            pointers_.push_back(token.data());
        }
    }

    [[nodiscard]] Tina::Core::ArgScanner scanner() noexcept
    {
        return Tina::Core::ArgScanner(static_cast<int>(pointers_.size()), pointers_.data());
    }

  private:
    std::vector<std::string> storage_;
    std::vector<char*> pointers_;
};

[[nodiscard]] Tina::Core::Result<bool> parseOne(std::string_view text,
                                               Detail::EditorSourceImportLaunchOptions& options)
{
    OwnedArgv argv({std::string{text}});
    auto scanner = argv.scanner();
    if (!scanner.next()) {
        return false;
    }
    return Detail::parseEditorSourceImportLaunchOption(scanner, options);
}

} // namespace

TEST(EditorSourceImportLaunchOptionsTests, RetainsMixedIntendedUnitSetInCallerOrder)
{
    Detail::EditorSourceImportLaunchOptions options{};
    const std::array arguments{
        argument("--project-root=", ProjectRoot),
        argument("--import-recipe=", RecipePath),
        argument("--import-gltf=", GltfPath),
        std::string{"--import-on-start"},
    };
    for (const auto& current : arguments) {
        auto parsed = parseOne(current, options);
        ASSERT_TRUE(parsed);
        EXPECT_TRUE(*parsed);
    }
    ASSERT_TRUE(Detail::validateEditorSourceImportLaunchOptions(options));

    ASSERT_EQ(options.intendedUnits.size(), 2U);
    EXPECT_EQ(options.intendedUnits[0].kind,
              Detail::EditorSourceImportLaunchUnitKind::CatalogRecipe);
    EXPECT_EQ(options.intendedUnits[1].kind,
              Detail::EditorSourceImportLaunchUnitKind::Gltf);
    EXPECT_TRUE(options.importOnStart);
}

TEST(EditorSourceImportLaunchOptionsTests, RejectsDuplicateUnitWithoutChangingSet)
{
    Detail::EditorSourceImportLaunchOptions options{};
    auto initial = parseOne(argument("--import-gltf=", GltfPath), options);
    ASSERT_TRUE(initial);
    ASSERT_TRUE(*initial);

    const auto duplicate = parseOne(argument("--import-gltf=", GltfPath), options);

    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, Tina::Core::CoreErrorCode::InvalidArgument);
    EXPECT_EQ(options.intendedUnits.size(), 1U);
}

TEST(EditorSourceImportLaunchOptionsTests, AutomaticImportRequiresProjectAndUnits)
{
    Detail::EditorSourceImportLaunchOptions options{};
    auto parsed = parseOne("--import-on-start", options);
    ASSERT_TRUE(parsed);
    ASSERT_TRUE(*parsed);

    EXPECT_FALSE(Detail::validateEditorSourceImportLaunchOptions(options));
}

TEST(EditorSourceImportLaunchOptionsTests, RejectsEquivalentDuplicatePathsWithoutMutation)
{
    Detail::EditorSourceImportLaunchOptions options{};
    auto initial = parseOne(argument("--import-gltf=", GltfPath), options);
    ASSERT_TRUE(initial);
    ASSERT_TRUE(*initial);

#if defined(_WIN32)
    constexpr std::string_view equivalent =
        "c:\\TINAPROJECT\\Source\\nested\\..\\hero.glb";
#else
    constexpr std::string_view equivalent =
        "/TinaProject/Source/nested/../hero.glb";
#endif
    const auto duplicate = parseOne(argument("--import-gltf=", equivalent), options);

    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, Tina::Core::CoreErrorCode::InvalidArgument);
    ASSERT_EQ(options.intendedUnits.size(), 1U);
    EXPECT_EQ(options.intendedUnits.front().pathUtf8, GltfPath);
}

TEST(EditorSourceImportLaunchOptionsTests, RejectsUnitsOutsideProjectSource)
{
    Detail::EditorSourceImportLaunchOptions options{};
    ASSERT_TRUE(parseOne(argument("--project-root=", ProjectRoot), options));
#if defined(_WIN32)
    constexpr std::string_view outside = "C:/TinaProject/External/hero.glb";
#else
    constexpr std::string_view outside = "/TinaProject/External/hero.glb";
#endif
    ASSERT_TRUE(parseOne(argument("--import-gltf=", outside), options));

    const auto validated = Detail::validateEditorSourceImportLaunchOptions(options);

    ASSERT_FALSE(validated);
    EXPECT_EQ(validated.error().code, Tina::Core::CoreErrorCode::PermissionDenied);
}

// New with the scanner: the value may arrive as a separate token. The editor gates all use the
// --name=value spelling, so this is the form that had no coverage before.
TEST(EditorSourceImportLaunchOptionsTests, AcceptsAValueGivenAsASeparateToken)
{
    Detail::EditorSourceImportLaunchOptions options{};
    OwnedArgv argv({std::string{"--project-root"}, std::string{ProjectRoot}});
    auto scanner = argv.scanner();
    ASSERT_TRUE(scanner.next());

    const auto parsed = Detail::parseEditorSourceImportLaunchOption(scanner, options);

    ASSERT_TRUE(parsed);
    EXPECT_TRUE(*parsed);
    EXPECT_EQ(options.projectRootUtf8, ProjectRoot);
}

// A trailing option with no value must not be mistaken for an unknown one. The parser answers false
// here, and the scanner is what tells the caller why.
TEST(EditorSourceImportLaunchOptionsTests, TrailingOptionWithNoValueIsReportedByTheScanner)
{
    Detail::EditorSourceImportLaunchOptions options{};
    OwnedArgv argv({std::string{"--project-root"}});
    auto scanner = argv.scanner();
    ASSERT_TRUE(scanner.next());

    const auto parsed = Detail::parseEditorSourceImportLaunchOption(scanner, options);

    ASSERT_TRUE(parsed);
    EXPECT_FALSE(*parsed);
    EXPECT_TRUE(scanner.failed());
    EXPECT_EQ(scanner.failedOption(), "--project-root");
    EXPECT_TRUE(options.projectRootUtf8.empty());
}

TEST(EditorSourceImportLaunchOptionsTests, RejectsRelativeAndMismatchedUnitPaths)
{
    Detail::EditorSourceImportLaunchOptions options{};
    const auto relative = parseOne("--import-gltf=Source/hero.glb", options);
    ASSERT_FALSE(relative);
    EXPECT_TRUE(options.intendedUnits.empty());

    const auto mismatched = parseOne(argument("--import-recipe=", GltfPath), options);
    ASSERT_FALSE(mismatched);
    EXPECT_TRUE(options.intendedUnits.empty());
}
