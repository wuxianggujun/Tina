set(TINA_ASSET_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetStore.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetSystem.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetGpuUpload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetRetirement.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetTypedViews.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetSpriteRender.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetGpuTexture.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/TileMapInstance.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/GridCollision.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/TileChunkView.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/TileChunkRender.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/TileChunkDirtyCache.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CharacterController2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogCook.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogFile.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogLoadOrder.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogLoadPlan.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogPackage.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogPackageLoad.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogPackagePublish.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogPackageSummary.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogPackageValidation.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogSnapshot.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CookedAssetBatch.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CookedAssetFile.hpp
)

set(TINA_ASSET_SOURCES
    AssetStore.cpp
    AssetSystem.cpp
    AssetGpuUpload.cpp
    AssetRetirement.cpp
    AssetTypedViews.cpp
    AssetSpriteRender.cpp
    AssetGpuTexture.cpp
    TileMapInstance.cpp
    GridCollision.cpp
    TileChunkView.cpp
    TileChunkRender.cpp
    TileChunkDirtyCache.cpp
    CharacterController2D.cpp
    CatalogCook.cpp
    CatalogFile.cpp
    CatalogLoadOrder.cpp
    CatalogLoadPlan.cpp
    CatalogPackage.cpp
    CatalogPackageLoad.cpp
    CatalogPackagePublish.cpp
    CatalogPackageSummary.cpp
    CatalogPackageValidation.cpp
    CatalogSnapshot.cpp
    CookedAssetBatch.cpp
    CookedAssetFile.cpp
)

if (TINA_BUILD_PHYSICS2D)
    list(APPEND TINA_ASSET_PUBLIC_HEADERS
        ${PROJECT_SOURCE_DIR}/include/tina/asset/TileMapPhysicsSync.hpp)
    list(APPEND TINA_ASSET_SOURCES TileMapPhysicsSync.cpp)
endif ()

add_library(tina_asset STATIC
    ${TINA_ASSET_PUBLIC_HEADERS}
    ${TINA_ASSET_SOURCES}
)
add_library(Tina::Asset ALIAS tina_asset)

target_compile_features(tina_asset PUBLIC cxx_std_23)
target_include_directories(tina_asset PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_asset
    PUBLIC Tina::Core Tina::AssetFormat Tina::Task Tina::Render
    PRIVATE Tina::ProjectOptions
)
if (TINA_BUILD_PHYSICS2D)
    target_link_libraries(tina_asset PUBLIC Tina::Physics2D)
endif ()
set_target_properties(tina_asset PROPERTIES CXX_EXTENSIONS OFF)
