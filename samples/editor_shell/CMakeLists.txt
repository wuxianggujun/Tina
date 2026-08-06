add_executable(tina_sample_editor_shell
    main.cpp
)

target_compile_features(tina_sample_editor_shell PRIVATE cxx_std_23)
target_link_libraries(tina_sample_editor_shell PRIVATE
    Tina::ProjectOptions
    Tina::DesktopBootstrap
    Tina::Editor
    Tina::Runtime
    Tina::Scene
)
set_target_properties(tina_sample_editor_shell PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
    add_custom_command(TARGET tina_sample_editor_shell POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tina_sample_editor_shell>
            $<TARGET_FILE_DIR:tina_sample_editor_shell>
        COMMAND_EXPAND_LISTS
    )
endif()
