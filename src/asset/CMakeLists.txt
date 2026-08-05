set(TINA_ASSET_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetFrameResourceResolver.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetHandle.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetStore.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetSystem.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetGpuUpload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetRetirement.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetTypedViews.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetSpriteRender.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetGpuTexture.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetGpuEnvironmentMap.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetGpuMesh.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/Mesh3DBindingRegistry.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/Sprite2DBindingRegistry.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/TileMapInstance.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/TileMapStream.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/GridCollision.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/TileChunkView.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/TileChunkRender.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/TileChunkDirtyCache.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CharacterController2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogCook.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogChangePlan.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/GltfCook.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogFile.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogLoadOrder.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogLoadPlan.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogPackage.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogPackageChangeDetector.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogPackageLoad.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogPackagePublish.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogPackageSummary.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogPackageValidation.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogSnapshot.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/SourceImportPlan.hpp
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
    AssetGpuEnvironmentMap.cpp
    AssetGpuMesh.cpp
    Mesh3DBindingRegistry.cpp
    Sprite2DBindingRegistry.cpp
    TileMapInstance.cpp
    TileMapStream.cpp
    GridCollision.cpp
    TileChunkView.cpp
    TileChunkRender.cpp
    TileChunkDirtyCache.cpp
    CharacterController2D.cpp
    CatalogCook.cpp
    CatalogChangePlan.cpp
    GltfFileSnapshot.cpp
    GltfCook.cpp
    CatalogFile.cpp
    CatalogLoadOrder.cpp
    CatalogLoadPlan.cpp
    CatalogPackage.cpp
    CatalogPackageChangeDetector.cpp
    CatalogPackageLoad.cpp
    CatalogPackagePublish.cpp
    CatalogPackageSummary.cpp
    CatalogPackageValidation.cpp
    CatalogSnapshot.cpp
    SourceImportPlan.cpp
    CookedAssetBatch.cpp
    CookedAssetFile.cpp
    Utf8Path.hpp
)

add_library(tina_asset_types INTERFACE)
add_library(Tina::AssetTypes ALIAS tina_asset_types)
target_sources(tina_asset_types INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/tina/asset/AssetFrameResourceResolver.hpp>"
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/tina/asset/AssetHandle.hpp>"
)
target_compile_features(tina_asset_types INTERFACE cxx_std_23)
target_include_directories(tina_asset_types INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_asset_types INTERFACE Tina::Core Tina::Render)

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
target_include_directories(tina_asset PRIVATE
    ${PROJECT_SOURCE_DIR}/thirdparty/cgltf
    ${PROJECT_SOURCE_DIR}/thirdparty/stb
)
target_link_libraries(tina_asset
    PUBLIC Tina::Core Tina::AssetTypes Tina::AssetFormat Tina::Task Tina::Render
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>" mikktspace::mikktspace
)
if (TINA_BUILD_PHYSICS2D)
    target_link_libraries(tina_asset PUBLIC Tina::Physics2D)
endif ()
set_target_properties(tina_asset PROPERTIES CXX_EXTENSIONS OFF)
