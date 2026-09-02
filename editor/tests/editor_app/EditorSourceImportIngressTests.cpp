#include "EditorSourceImportIngress.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace Detail = Tina::EditorApp::Detail;

namespace {

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] std::string canonicalPathToUtf8(const std::filesystem::path& path)
{
    return pathToUtf8(std::filesystem::weakly_canonical(path));
}

[[nodiscard]] std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

class EditorSourceImportIngressTests : public testing::Test {
protected:
    void SetUp() override
    {
        static std::atomic_uint64_t nextId{0};
        root_ = std::filesystem::temp_directory_path() /
                ("tina-editor-source-ingress-" +
                 std::to_string(nextId.fetch_add(1, std::memory_order_relaxed)));
        sourceRoot_ = root_ / "Project" / "Source";
        externalRoot_ = root_ / "External";
        ASSERT_TRUE(std::filesystem::create_directories(sourceRoot_));
        ASSERT_TRUE(std::filesystem::create_directories(externalRoot_));
    }

    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] std::filesystem::path writeFile(
        const std::filesystem::path& root,
        const std::filesystem::path& relativePath,
        std::string_view contents)
    {
        const auto path = root / relativePath;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        EXPECT_TRUE(output.good());
        return path;
    }

    std::filesystem::path root_{};
    std::filesystem::path sourceRoot_{};
    std::filesystem::path externalRoot_{};
};

TEST_F(EditorSourceImportIngressTests, CopiesExternalImageAndAudioIntoProject)
{
    const auto image = writeFile(externalRoot_, "portrait.PNG", "png fixture");
    const auto audio = writeFile(externalRoot_, "voice.wav", "wav fixture");
    const std::array selectedPaths{pathToUtf8(image), pathToUtf8(audio)};
    const auto projectImage = sourceRoot_ / "Imported" / "Images" / "portrait.PNG";
    const auto projectAudio = sourceRoot_ / "Imported" / "Audio" / "voice.wav";

    {
        auto ingress = Detail::prepareEditorSourceImportIngress(
            pathToUtf8(sourceRoot_), selectedPaths,
            Detail::EditorSourceImportUnitCapacity, {}, {});

        ASSERT_TRUE(ingress) << ingress.error().message;
        EXPECT_EQ(ingress->copiedFileCount(), 2U);
        EXPECT_EQ(ingress->reusedFileCount(), 0U);
        ASSERT_EQ(ingress->projectPathsUtf8().size(), 2U);
        EXPECT_EQ(ingress->projectPathsUtf8()[0], canonicalPathToUtf8(projectImage));
        EXPECT_EQ(ingress->projectPathsUtf8()[1], canonicalPathToUtf8(projectAudio));
        EXPECT_EQ(readFile(projectImage), "png fixture");
        EXPECT_EQ(readFile(projectAudio), "wav fixture");
        ingress->commit();
    }

    EXPECT_TRUE(std::filesystem::is_regular_file(projectImage));
    EXPECT_TRUE(std::filesystem::is_regular_file(projectAudio));
}

TEST_F(EditorSourceImportIngressTests, RollsBackUncommittedFilesAndDirectories)
{
    const auto image = writeFile(externalRoot_, "rollback.png", "rollback fixture");
    const std::array selectedPaths{pathToUtf8(image)};
    const auto importedRoot = sourceRoot_ / "Imported";
    const auto projectImage = importedRoot / "Images" / "rollback.png";

    {
        auto ingress = Detail::prepareEditorSourceImportIngress(
            pathToUtf8(sourceRoot_), selectedPaths,
            Detail::EditorSourceImportUnitCapacity, {}, {});
        ASSERT_TRUE(ingress) << ingress.error().message;
        ASSERT_TRUE(std::filesystem::is_regular_file(projectImage));
    }

    EXPECT_FALSE(std::filesystem::exists(projectImage));
    EXPECT_FALSE(std::filesystem::exists(importedRoot));
}

TEST_F(EditorSourceImportIngressTests, PreflightRejectsExternalGltfBeforeCopyingAnything)
{
    const auto image = writeFile(externalRoot_, "valid.png", "valid image");
    const auto gltf = writeFile(externalRoot_, "scene.gltf", "{}");
    const std::array selectedPaths{pathToUtf8(image), pathToUtf8(gltf)};

    auto ingress = Detail::prepareEditorSourceImportIngress(
        pathToUtf8(sourceRoot_), selectedPaths,
        Detail::EditorSourceImportUnitCapacity, {}, {});

    ASSERT_FALSE(ingress);
    EXPECT_EQ(ingress.error().code, Tina::Core::CoreErrorCode::PermissionDenied);
    EXPECT_FALSE(std::filesystem::exists(sourceRoot_ / "Imported"));
}

TEST_F(EditorSourceImportIngressTests, KeepsProjectGltfAtItsExistingSourcePath)
{
    const auto gltf = writeFile(sourceRoot_, "Models/scene.gltf", "{}");
    const std::array selectedPaths{pathToUtf8(gltf)};

    auto ingress = Detail::prepareEditorSourceImportIngress(
        pathToUtf8(sourceRoot_), selectedPaths,
        Detail::EditorSourceImportUnitCapacity, {}, {});

    ASSERT_TRUE(ingress) << ingress.error().message;
    EXPECT_EQ(ingress->copiedFileCount(), 0U);
    ASSERT_EQ(ingress->projectPathsUtf8().size(), 1U);
    EXPECT_EQ(ingress->projectPathsUtf8().front(), canonicalPathToUtf8(gltf));
    ingress->commit();
}

TEST_F(EditorSourceImportIngressTests, PreservesDifferentExistingFileAndUsesSuffix)
{
    const auto existing = writeFile(
        sourceRoot_, "Imported/Images/icon.png", "existing image");
    const auto external = writeFile(externalRoot_, "icon.png", "replacement image");
    const std::array selectedPaths{pathToUtf8(external)};
    const auto suffixed = sourceRoot_ / "Imported" / "Images" / "icon_2.png";

    auto ingress = Detail::prepareEditorSourceImportIngress(
        pathToUtf8(sourceRoot_), selectedPaths,
        Detail::EditorSourceImportUnitCapacity, {}, {});

    ASSERT_TRUE(ingress) << ingress.error().message;
    EXPECT_EQ(ingress->copiedFileCount(), 1U);
    ASSERT_EQ(ingress->projectPathsUtf8().size(), 1U);
    EXPECT_EQ(ingress->projectPathsUtf8().front(), canonicalPathToUtf8(suffixed));
    EXPECT_EQ(readFile(existing), "existing image");
    EXPECT_EQ(readFile(suffixed), "replacement image");
    ingress->commit();
}

TEST_F(EditorSourceImportIngressTests, ReusesSameNameWithIdenticalContent)
{
    const auto existing = writeFile(
        sourceRoot_, "Imported/Images/shared.jpeg", "shared image");
    const auto external = writeFile(externalRoot_, "shared.jpeg", "shared image");
    const std::array selectedPaths{pathToUtf8(external)};

    auto ingress = Detail::prepareEditorSourceImportIngress(
        pathToUtf8(sourceRoot_), selectedPaths,
        Detail::EditorSourceImportUnitCapacity, {}, {});

    ASSERT_TRUE(ingress) << ingress.error().message;
    EXPECT_EQ(ingress->copiedFileCount(), 0U);
    EXPECT_EQ(ingress->reusedFileCount(), 1U);
    ASSERT_EQ(ingress->projectPathsUtf8().size(), 1U);
    EXPECT_EQ(ingress->projectPathsUtf8().front(), canonicalPathToUtf8(existing));
    ingress->commit();
}

TEST_F(EditorSourceImportIngressTests, CopiesRepeatedPhysicalSelectionOnlyOnce)
{
    const auto external = writeFile(externalRoot_, "repeat.jpg", "repeated image");
    const std::array selectedPaths{pathToUtf8(external), pathToUtf8(external)};

    auto ingress = Detail::prepareEditorSourceImportIngress(
        pathToUtf8(sourceRoot_), selectedPaths,
        Detail::EditorSourceImportUnitCapacity, {}, {});

    ASSERT_TRUE(ingress) << ingress.error().message;
    EXPECT_EQ(ingress->copiedFileCount(), 1U);
    ASSERT_EQ(ingress->projectPathsUtf8().size(), 2U);
    EXPECT_EQ(ingress->projectPathsUtf8()[0], ingress->projectPathsUtf8()[1]);
    ingress->commit();
}

} // namespace
