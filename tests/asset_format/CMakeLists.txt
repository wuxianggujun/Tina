set(TINA_ASSET_FORMAT_TEST_SOURCES
    AssetFormatTests.cpp
    AnimationClip3DPayloadTests.cpp
    AudioClipPayloadTests.cpp
    EnvironmentMapPayloadTests.cpp
    Fx2DPayloadTests.cpp
    MaterialPayloadTests.cpp
    NavigationGrid2DPayloadTests.cpp
    PrefabPayloadTests.cpp
    SpriteAnimationClipPayloadTests.cpp
    SourceImportMetadataFormatTests.cpp
    SkinnedMeshPayloadTests.cpp
    StaticMeshPayloadTests.cpp
    TextureSpritePayloadTests.cpp
    TileMapTilesetPayloadTests.cpp
    TileMapChunkPayloadTests.cpp
    TypedPayloadMalformedCorpusTests.cpp
    World2DSnapshotTests.cpp
    header_isolation/AssetFormatErrorsHeader.cpp
    header_isolation/AssetFormatHeader.cpp
    header_isolation/AnimationClip3DPayloadHeader.cpp
    header_isolation/AudioClipPayloadHeader.cpp
    header_isolation/EnvironmentMapPayloadHeader.cpp
    header_isolation/Fx2DPayloadHeader.cpp
    header_isolation/MaterialPayloadHeader.cpp
    header_isolation/NavigationGrid2DPayloadHeader.cpp
    header_isolation/PrefabPayloadHeader.cpp
    header_isolation/SpriteAnimationClipPayloadHeader.cpp
    header_isolation/SpritePayloadHeader.cpp
    header_isolation/SourceImportMetadataFormatHeader.cpp
    header_isolation/SkinnedMeshPayloadHeader.cpp
    header_isolation/StaticMeshPayloadHeader.cpp
    header_isolation/Texture2DPayloadHeader.cpp
    header_isolation/TilesetPayloadHeader.cpp
    header_isolation/TileMapPayloadHeader.cpp
    header_isolation/TileMapChunkPayloadHeader.cpp
    header_isolation/World2DSnapshotHeader.cpp
)

add_executable(tina_asset_format_tests ${TINA_ASSET_FORMAT_TEST_SOURCES})
target_compile_features(tina_asset_format_tests PRIVATE cxx_std_23)
target_include_directories(tina_asset_format_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(tina_asset_format_tests PRIVATE
    Tina::ProjectOptions
    Tina::AssetFormat
    GTest::gtest_main
)
set_target_properties(tina_asset_format_tests PROPERTIES CXX_EXTENSIONS OFF)
