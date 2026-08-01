add_executable(tina_sample_ui_showcase
    ShowcaseImageFixture.hpp
    ShowcaseRenderDevice.hpp
    ShowcaseUI.cpp
    ShowcaseUI.hpp
    main.cpp
)

target_compile_features(tina_sample_ui_showcase PRIVATE cxx_std_23)
target_include_directories(tina_sample_ui_showcase PRIVATE
    "${PROJECT_SOURCE_DIR}/samples/common"
)
target_link_libraries(tina_sample_ui_showcase PRIVATE
    Tina::ProjectOptions
    Tina::DesktopBootstrap
    Tina::Runtime
)
set_target_properties(tina_sample_ui_showcase PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
    add_custom_command(TARGET tina_sample_ui_showcase POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tina_sample_ui_showcase>
            $<TARGET_FILE_DIR:tina_sample_ui_showcase>
        COMMAND_EXPAND_LISTS
    )
endif()
