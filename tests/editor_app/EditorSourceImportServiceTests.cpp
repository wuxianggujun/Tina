#include "EditorSourceImportService.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace Detail = Tina::EditorApp::Detail;

namespace {

Detail::EditorSourceImportRequest makeRequest()
{
    return {
        .sourceRootUtf8 = "C:/Project/Source",
        .baselineCatalogRootUtf8 = "C:/Project/Catalog",
        .baselineStatePathUtf8 = "C:/Project/.tina/cache/source-import/import-state.tmeta",
        .freshStageRootUtf8 = "C:/Project/.tina/cache/source-import/stages/1/catalog",
        .freshStageStatePathUtf8 =
            "C:/Project/.tina/cache/source-import/stages/1/import-state.tmeta",
        .targetPlatform = Tina::AssetFormat::TargetPlatform::WindowsX64,
        .units = {{
            .kind = Detail::EditorSourceImportUnitKind::CatalogRecipe,
            .sourcePathUtf8 = "C:/Project/Source/game.recipe",
        }},
    };
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

} // namespace

TEST(EditorSourceImportServiceTests, ReadyStagePersistsUntilAcknowledged)
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
    ASSERT_TRUE(service.acknowledgeReady());
    EXPECT_EQ(service.state(), Detail::EditorSourceImportServiceState::Idle);
}

TEST(EditorSourceImportServiceTests, WorkerFailureIsOwnedUntilDismissed)
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
