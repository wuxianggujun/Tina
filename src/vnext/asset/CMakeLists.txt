set(TINA_ASSET_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/asset/AssetErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/asset/CatalogSnapshot.hpp
)

add_library(tina_asset STATIC
    ${TINA_ASSET_PUBLIC_HEADERS}
    CatalogSnapshot.cpp
)
add_library(Tina::Asset ALIAS tina_asset)

target_compile_features(tina_asset PUBLIC cxx_std_23)
target_include_directories(tina_asset PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_asset
    PUBLIC Tina::Core Tina::AssetFormat
    PRIVATE Tina::ProjectOptions
)
set_target_properties(tina_asset PROPERTIES CXX_EXTENSIONS OFF)
