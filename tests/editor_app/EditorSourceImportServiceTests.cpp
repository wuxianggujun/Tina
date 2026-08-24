#include "EditorSourceImportService.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace Detail = Tina::EditorApp::Detail;

namespace {

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

void pollUntilSettled(Detail::EditorSourceImportService& service)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline) {
        ASSERT_TRUE(service.poll());
        if (service.state() != Detail::EditorSourceImportServiceState::Running) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    FAIL() << "source import worker did not settle";
}

void writePngFixture(const std::filesystem::path& path)
{
    constexpr std::array<std::uint8_t, 120> bytes{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x01, 0x73, 0x52, 0x47, 0x42, 0x00, 0xae, 0xce, 0x1c, 0xe9, 0x00, 0x00,
        0x00, 0x04, 0x67, 0x41, 0x4d, 0x41, 0x00, 0x00, 0xb1, 0x8f, 0x0b, 0xfc,
        0x61, 0x05, 0x00, 0x00, 0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00,
        0x0e, 0xc3, 0x00, 0x00, 0x0e, 0xc3, 0x01, 0xc7, 0x6f, 0xa8, 0x64, 0x00,
        0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x18, 0x57, 0x63, 0xf8, 0xff,
        0xff, 0xff, 0x7f, 0x00, 0x09, 0xfb, 0x03, 0xfd, 0x05, 0x43, 0x45, 0xca,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

class EditorSourceImportServiceTests : public testing::Test {
protected:
    void SetUp() override
    {
        static std::atomic_uint64_t nextId{0};
        root_ = std::filesystem::temp_directory_path() /
                ("tina-editor-source-service-" +
                 std::to_string(nextId.fetch_add(1, std::memory_order_relaxed)));
        sourceRoot_ = root_ / "Source";
        externalRoot_ = root_ / "External";
        ASSERT_TRUE(std::filesystem::create_directories(sourceRoot_));
        ASSERT_TRUE(std::filesystem::create_directories(externalRoot_));
        ASSERT_TRUE(std::filesystem::create_directories(root_ / "stage"));
        recipePath_ = sourceRoot_ / "game.recipe";
        std::ofstream recipe{recipePath_, std::ios::binary | std::ios::trunc};
        recipe << "fixture";
        ASSERT_TRUE(recipe.good());
    }

    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] Detail::EditorSourceImportRequest makeRequest() const
    {
        return {
            .sourceRootUtf8 = pathToUtf8(sourceRoot_),
            .baselineCatalogRootUtf8 = pathToUtf8(root_ / "Catalog"),
            .baselineStatePathUtf8 = pathToUtf8(root_ / "baseline.tmeta"),
            .freshStageRootUtf8 = pathToUtf8(root_ / "stage" / "Catalog"),
            .freshStageStatePathUtf8 = pathToUtf8(root_ / "stage" / "import-state.tmeta"),
            .targetPlatform = Tina::AssetFormat::TargetPlatform::WindowsX64,
            .units = {{
                .kind = Detail::EditorSourceImportUnitKind::CatalogRecipe,
                .sourcePathUtf8 = pathToUtf8(recipePath_),
            }},
        };
    }

    std::filesystem::path root_{};
    std::filesystem::path sourceRoot_{};
    std::filesystem::path externalRoot_{};
    std::filesystem::path recipePath_{};
};

} // namespace

TEST_F(EditorSourceImportServiceTests, ReadyStagePersistsUntilCommitted)
{
    Detail::EditorSourceImportService service{
        [](const Detail::EditorSourceImportRequest& request, std::stop_token) {
            EXPECT_EQ(request.units.size(), 1U);
            return Tina::Core::Result<Detail::EditorSourceImportWorkResult>{
                Detail::EditorSourceImportWorkResult{
                    .statistics = {
                        .mode = Detail::EditorSourceImportMode::FullRecook,
                        .unitsTotal = 1,
                        .unitsRecooked = 1,
                        .objectsCooked = 2,
                    },
                    .stageCreated = true,
                    .unitOutputs = {{
                        .unitId = *Tina::AssetFormat::SourceImportUnitId::parseCanonical(
                            "01000000000000000000000000000001"),
                        .sourceUtf8Path = request.units.front().sourcePathUtf8,
                    }},
                }};
        }};

    ASSERT_TRUE(service.start(makeRequest()));
    pollUntilSettled(service);
    ASSERT_EQ(service.state(), Detail::EditorSourceImportServiceState::Ready);
    ASSERT_NE(service.readyStage(), nullptr);
    EXPECT_EQ(service.readyStage()->statistics.objectsCooked, 2U);
    ASSERT_TRUE(service.poll());
    EXPECT_NE(service.readyStage(), nullptr);
    ASSERT_TRUE(service.commitReady());
    EXPECT_EQ(service.state(), Detail::EditorSourceImportServiceState::Idle);
}

TEST_F(EditorSourceImportServiceTests, WorkerFailureIsOwnedUntilDismissed)
{
    Detail::EditorSourceImportService service{
        [](const Detail::EditorSourceImportRequest&, std::stop_token)
            -> Tina::Core::Result<Detail::EditorSourceImportWorkResult> {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                       "fixture cook failed");
        }};

    ASSERT_TRUE(service.start(makeRequest()));
    pollUntilSettled(service);
    ASSERT_EQ(service.state(), Detail::EditorSourceImportServiceState::Failed);
    ASSERT_NE(service.failure(), nullptr);
    EXPECT_EQ(service.failure()->message, "fixture cook failed");
    ASSERT_TRUE(service.dismissFailure());
    EXPECT_EQ(service.state(), Detail::EditorSourceImportServiceState::Idle);
}

TEST_F(EditorSourceImportServiceTests, RealPngIngressCooksValidatedReadyCatalog)
{
    const auto selectedPng = externalRoot_ / "selected.png";
    writePngFixture(selectedPng);

    Detail::EditorSourceImportService service{
        Detail::makeEditorSourceImportPipelineWorker()};
    auto request = makeRequest();
    request.units.clear();
    request.selectedPathsUtf8 = {pathToUtf8(selectedPng)};

    ASSERT_TRUE(service.start(std::move(request)));
    pollUntilSettled(service);
    ASSERT_EQ(service.state(), Detail::EditorSourceImportServiceState::Ready)
        << (service.failure() != nullptr ? service.failure()->message : "missing failure");
    const auto* ready = service.readyStage();
    ASSERT_NE(ready, nullptr);
    ASSERT_TRUE(ready->catalog);
    EXPECT_TRUE(ready->stageCreated);
    EXPECT_EQ(ready->selectedPathCount, 1U);
    EXPECT_EQ(ready->addedUnitCount, 1U);
    EXPECT_EQ(ready->copiedFileCount, 1U);
    EXPECT_EQ(ready->intendedUnits.size(), 1U);
    EXPECT_EQ(ready->intendedUnits.front().kind,
              Detail::EditorSourceImportUnitKind::Texture);
    EXPECT_EQ(ready->statistics.unitsTotal, 1U);
    EXPECT_EQ(ready->statistics.unitsRecooked, 1U);
    EXPECT_GT(ready->statistics.objectsCooked, 0U);
    ASSERT_EQ(ready->unitOutputs.size(), 1U);
    EXPECT_EQ(ready->unitOutputs.front().sourceUtf8Path,
              ready->intendedUnits.front().sourcePathUtf8);
    ASSERT_EQ(ready->unitOutputs.front().outputs.size(), 1U);
    EXPECT_EQ(ready->unitOutputs.front().outputs.front().assetKind,
              Tina::AssetFormat::AssetKind::Texture2D);

    auto committedUnits = service.commitReady();
    ASSERT_TRUE(committedUnits);
    EXPECT_EQ(committedUnits->size(), 1U);
    EXPECT_EQ(service.state(), Detail::EditorSourceImportServiceState::Idle);
    EXPECT_TRUE(std::filesystem::is_regular_file(
        sourceRoot_ / "Imported" / "Images" / "selected.png"));
}
