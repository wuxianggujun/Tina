#include "EditorSourceImportService.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
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
    for (int attempt = 0; attempt < 100; ++attempt) {
        ASSERT_TRUE(service.poll());
        if (service.state() != Detail::EditorSourceImportServiceState::Running) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    FAIL() << "source import worker did not settle";
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
        ASSERT_TRUE(std::filesystem::create_directories(sourceRoot_));
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
