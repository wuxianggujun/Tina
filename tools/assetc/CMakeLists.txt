add_executable(tina_assetc
    main.cpp
    ShaderCompile.cpp
)
target_compile_features(tina_assetc PRIVATE cxx_std_23)
# Reaches src/core/io/PathUtil.hpp; see the note in src/asset/CMakeLists.txt.
target_include_directories(tina_assetc PRIVATE ${PROJECT_SOURCE_DIR}/src)
# No TinaBgfxRuntime::l-smash here. The cooker has no MP4 path -- no source in tools/assetc or
# src/asset includes an l-smash header or names an lsmash_ symbol -- and that target only exists
# when TINA_BUILD_RENDER_BGFX is on, so naming it made a Null preset fail to generate with
# "links to TinaBgfxRuntime::l-smash but the target was not found". Add it back together with
# the code that demuxes, guarded on the target existing.
target_link_libraries(tina_assetc
    PRIVATE
        Tina::ProjectOptions
        Tina::Asset
)
set_target_properties(tina_assetc PROPERTIES CXX_EXTENSIONS OFF)
