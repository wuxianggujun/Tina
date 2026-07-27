add_executable(tina_sample_3d
    main.cpp
    DeviceCapture.hpp
    Product3DUI.cpp
    Product3DUI.hpp
)

target_compile_features(tina_sample_3d PRIVATE cxx_std_23)
target_include_directories(tina_sample_3d PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/samples/common"
)
target_link_libraries(tina_sample_3d PRIVATE
    Tina::ProjectOptions
    Tina::DesktopBootstrap
    Tina::Runtime
    Tina::Asset
    Tina::Scene
    Tina::RenderBgfx
)
set_target_properties(tina_sample_3d PROPERTIES CXX_EXTENSIONS OFF)

set(_tina_complete_pbr_gltf
    "${PROJECT_SOURCE_DIR}/tests/fixtures/gltf/complete_pbr/complete_pbr.gltf")
if(EXISTS "${_tina_complete_pbr_gltf}")
    # Default product gate uses the complete PBR repo fixture (multi-mesh + MR/normal maps).
    target_compile_definitions(tina_sample_3d PRIVATE
        "TINA_COMPLETE_PBR_GLTF_FIXTURE=\"${_tina_complete_pbr_gltf}\"")
endif()

if(WIN32)
    add_custom_command(TARGET tina_sample_3d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tina_sample_3d>
            $<TARGET_FILE_DIR:tina_sample_3d>
        COMMAND_EXPAND_LISTS
    )
endif()
