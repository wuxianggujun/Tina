#include "EditorFileDialogLinux.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <sys/stat.h>

namespace Detail = Tina::EditorApp::Detail;

namespace {

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

class EditorFileDialogLinuxTests final : public testing::Test {
protected:
    void SetUp() override
    {
        static std::uint64_t nextId = 0;
        root_ = std::filesystem::temp_directory_path() /
                ("tina-editor-file-dialog-linux-" + std::to_string(nextId++));
        helperRoot_ = root_ / "bin";
        initialDirectory_ = root_ / "initial";
        ASSERT_TRUE(std::filesystem::create_directories(helperRoot_));
        ASSERT_TRUE(std::filesystem::create_directories(initialDirectory_));

        const auto helperPath = helperRoot_ / "zenity";
        std::ofstream helper{helperPath, std::ios::binary | std::ios::trunc};
        ASSERT_TRUE(helper.good());
        helper << "#!/bin/sh\n"
                  "if [ \"${TINA_FAKE_DIALOG_CANCEL:-0}\" = \"1\" ]; then exit 1; fi\n"
                  "printf '%s\\n' \"${TINA_FAKE_DIALOG_OUTPUT:-}\"\n";
        helper.close();
        ASSERT_EQ(::chmod(helperPath.c_str(), 0700), 0);

        const char* currentPath = std::getenv("PATH");
        previousPath_ = currentPath != nullptr ? currentPath : "";
        const std::string testPath = pathToUtf8(helperRoot_) + ":" + previousPath_;
        ASSERT_EQ(::setenv("PATH", testPath.c_str(), 1), 0);
        ASSERT_EQ(::unsetenv("TINA_FAKE_DIALOG_CANCEL"), 0);
    }

    void TearDown() override
    {
        ASSERT_EQ(::setenv("PATH", previousPath_.c_str(), 1), 0);
        ASSERT_EQ(::unsetenv("TINA_FAKE_DIALOG_CANCEL"), 0);
        ASSERT_EQ(::unsetenv("TINA_FAKE_DIALOG_OUTPUT"), 0);
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    void setOutput(std::string_view output)
    {
        ASSERT_EQ(::setenv("TINA_FAKE_DIALOG_OUTPUT", std::string{output}.c_str(), 1), 0);
    }

    void cancel()
    {
        ASSERT_EQ(::setenv("TINA_FAKE_DIALOG_CANCEL", "1", 1), 0);
    }

    std::filesystem::path root_{};
    std::filesystem::path helperRoot_{};
    std::filesystem::path initialDirectory_{};
    std::string previousPath_{};
};

TEST_F(EditorFileDialogLinuxTests, SelectedPathIsReturnedAsUtf8)
{
    const auto selectedPath = initialDirectory_ / "scene.tworld";
    std::ofstream fixture{selectedPath};
    ASSERT_TRUE(fixture.good());
    setOutput(pathToUtf8(selectedPath));

    const auto result = Detail::openExistingFileLinux({
        .titleUtf8 = "Open",
        .initialDirectoryUtf8 = pathToUtf8(initialDirectory_),
    });

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_TRUE(result->selected());
    EXPECT_EQ(result->selectedPathUtf8, pathToUtf8(selectedPath));
}

TEST_F(EditorFileDialogLinuxTests, CancelIsASuccessNoOp)
{
    cancel();

    const auto result = Detail::openExistingFileLinux({
        .titleUtf8 = "Open",
        .initialDirectoryUtf8 = pathToUtf8(initialDirectory_),
    });

    ASSERT_TRUE(result) << result.error().message;
    EXPECT_FALSE(result->selected());
    EXPECT_TRUE(result->selectedPathUtf8.empty());
}

TEST_F(EditorFileDialogLinuxTests, SaveRejectsDirectorySelection)
{
    const auto selectedDirectory = initialDirectory_ / "existing.tworld";
    ASSERT_TRUE(std::filesystem::create_directories(selectedDirectory));
    setOutput(pathToUtf8(selectedDirectory));

    const auto result = Detail::saveFileLinux({
        .titleUtf8 = "Save",
        .initialDirectoryUtf8 = pathToUtf8(initialDirectory_),
        .suggestedFileNameUtf8 = "scene",
        .defaultExtensionUtf8 = "tworld",
    });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Tina::Core::CoreErrorCode::InvalidArgument);
}

TEST_F(EditorFileDialogLinuxTests, SaveAppendsDefaultExtensionToNewFile)
{
    const auto selectedPath = initialDirectory_ / "new-scene";
    setOutput(pathToUtf8(selectedPath));

    const auto result = Detail::saveFileLinux({
        .titleUtf8 = "Save",
        .initialDirectoryUtf8 = pathToUtf8(initialDirectory_),
        .suggestedFileNameUtf8 = "scene",
        .defaultExtensionUtf8 = "tworld",
    });

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_TRUE(result->selected());
    EXPECT_EQ(result->selectedPathUtf8,
              pathToUtf8(initialDirectory_ / "new-scene.tworld"));
}

TEST_F(EditorFileDialogLinuxTests, SaveRejectsRootSelectionBeforeAppendingExtension)
{
    setOutput("/");

    const auto result = Detail::saveFileLinux({
        .titleUtf8 = "Save",
        .initialDirectoryUtf8 = pathToUtf8(initialDirectory_),
        .suggestedFileNameUtf8 = "scene",
        .defaultExtensionUtf8 = "tworld",
    });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Tina::Core::CoreErrorCode::InvalidArgument);
}

TEST_F(EditorFileDialogLinuxTests, SaveRejectsPathBearingSuggestedName)
{
    const auto result = Detail::saveFileLinux({
        .titleUtf8 = "Save",
        .initialDirectoryUtf8 = pathToUtf8(initialDirectory_),
        .suggestedFileNameUtf8 = "nested/scene",
        .defaultExtensionUtf8 = "tworld",
    });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Tina::Core::CoreErrorCode::InvalidArgument);
}

TEST_F(EditorFileDialogLinuxTests, OpenRejectsRelativeHelperOutput)
{
    setOutput("relative.tworld");

    const auto result = Detail::openExistingFileLinux({
        .titleUtf8 = "Open",
        .initialDirectoryUtf8 = pathToUtf8(initialDirectory_),
    });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Tina::Core::CoreErrorCode::InvalidArgument);
}

} // namespace
