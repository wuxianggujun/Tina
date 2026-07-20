set(TINA_ASSET_TEST_SOURCES
    AssetStoreTests.cpp
    AssetSystemTests.cpp
    AssetSystemPumpTests.cpp
    AssetSystemAsyncPumpTests.cpp
    AssetGpuUploadTests.cpp
    AssetSystemGpuPipelineTests.cpp
    CatalogSnapshotTests.cpp
    CatalogLoadOrderTests.cpp
    CatalogLoadPlanTests.cpp
    CatalogPackageTests.cpp
    CatalogPackageLoadTests.cpp
    CatalogPackagePublishTests.cpp
    CatalogPackagePipelineTests.cpp
    CatalogPackageSummaryTests.cpp
    CatalogPackageValidationTests.cpp
    CookedAssetBatchTests.cpp
    CookedAssetFileTests.cpp
    header_isolation/AssetErrorsHeader.cpp
    header_isolation/AssetStoreHeader.cpp
    header_isolation/AssetSystemHeader.cpp
    header_isolation/AssetGpuUploadHeader.cpp
    header_isolation/CatalogFileHeader.cpp
    header_isolation/CatalogLoadOrderHeader.cpp
    header_isolation/CatalogLoadPlanHeader.cpp
    header_isolation/CatalogPackageHeader.cpp
    header_isolation/CatalogPackageLoadHeader.cpp
    header_isolation/CatalogPackagePublishHeader.cpp
    header_isolation/CatalogPackageSummaryHeader.cpp
    header_isolation/CatalogPackageValidationHeader.cpp
    header_isolation/CatalogSnapshotHeader.cpp
    header_isolation/CookedAssetBatchHeader.cpp
    header_isolation/CookedAssetFileHeader.cpp
)

add_executable(tina_asset_tests ${TINA_ASSET_TEST_SOURCES})
target_compile_features(tina_asset_tests PRIVATE cxx_std_23)
target_include_directories(tina_asset_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(tina_asset_tests PRIVATE
    Tina::ProjectOptions
    Tina::Asset
    Tina::Task
    GTest::gtest_main
)
set_target_properties(tina_asset_tests PROPERTIES CXX_EXTENSIONS OFF)
