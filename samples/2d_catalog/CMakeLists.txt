add_executable(tina_sample_2d_catalog
    main.cpp
)

target_compile_features(tina_sample_2d_catalog PRIVATE cxx_std_23)
target_include_directories(tina_sample_2d_catalog PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(tina_sample_2d_catalog PRIVATE
    Tina::ProjectOptions
    Tina::DesktopBootstrap
    Tina::Runtime
    Tina::Asset
    Tina::RenderBgfx
)
set_target_properties(tina_sample_2d_catalog PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
    add_custom_command(TARGET tina_sample_2d_catalog POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tina_sample_2d_catalog>
            $<TARGET_FILE_DIR:tina_sample_2d_catalog>
        COMMAND_EXPAND_LISTS
    )
endif()
