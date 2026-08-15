set(TINA_ASSET_FORMAT_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/AssetFormat.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/AssetFormatErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/AnimationClip3DPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/AudioClipPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/EnvironmentMapPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/Fx2DPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/MaterialPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/NavigationGrid2DPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/PrefabPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/SpriteAnimationClipPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/SpritePayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/SourceImportMetadataFormat.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/SkinnedMeshPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/StaticMeshPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/Texture2DPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/TilesetPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/TileMapPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/TileMapChunkPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/World2DSnapshot.hpp
)

add_library(tina_asset_format STATIC
    ${TINA_ASSET_FORMAT_PUBLIC_HEADERS}
    AssetFormat.cpp
    AnimationClip3DPayload.cpp
    AudioClipPayload.cpp
    EnvironmentMapPayload.cpp
    Fx2DPayload.cpp
    MaterialPayload.cpp
    NavigationGrid2DPayload.cpp
    PrefabPayload.cpp
    SpriteAnimationClipPayload.cpp
    SpritePayload.cpp
    SourceImportMetadataFormat.cpp
    SkinnedMeshPayload.cpp
    StaticMeshPayload.cpp
    Texture2DPayload.cpp
    TilesetPayload.cpp
    TileMapPayload.cpp
    TileMapChunkPayload.cpp
    World2DSnapshot.cpp
)
add_library(Tina::AssetFormat ALIAS tina_asset_format)

target_compile_features(tina_asset_format PUBLIC cxx_std_23)
target_include_directories(tina_asset_format PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_asset_format
    PUBLIC Tina::Core
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_asset_format PROPERTIES CXX_EXTENSIONS OFF)
