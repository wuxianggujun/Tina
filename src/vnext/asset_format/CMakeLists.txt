set(TINA_ASSET_FORMAT_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/AssetFormat.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/AssetFormatErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/AudioClipPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/MaterialPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/PrefabPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/SpritePayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/StaticMeshPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/Texture2DPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/TilesetPayload.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset_format/TileMapPayload.hpp
)

add_library(tina_asset_format STATIC
    ${TINA_ASSET_FORMAT_PUBLIC_HEADERS}
    AssetFormat.cpp
    AudioClipPayload.cpp
    MaterialPayload.cpp
    PrefabPayload.cpp
    SpritePayload.cpp
    StaticMeshPayload.cpp
    Texture2DPayload.cpp
    TilesetPayload.cpp
    TileMapPayload.cpp
)
add_library(Tina::AssetFormat ALIAS tina_asset_format)

target_compile_features(tina_asset_format PUBLIC cxx_std_23)
target_include_directories(tina_asset_format PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_asset_format
    PUBLIC Tina::Core
    PRIVATE Tina::ProjectOptions
)
set_target_properties(tina_asset_format PROPERTIES CXX_EXTENSIONS OFF)
