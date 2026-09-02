add_executable(tina_assetc
    main.cpp
)
target_compile_features(tina_assetc PRIVATE cxx_std_23)
# Reaches src/core/io/PathUtil.hpp; see the note in src/asset/CMakeLists.txt.
target_include_directories(tina_assetc PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(tina_assetc
    PRIVATE
        Tina::ProjectOptions
        Tina::Asset
        TinaBgfxRuntime::l-smash
)
set_target_properties(tina_assetc PROPERTIES CXX_EXTENSIONS OFF)
