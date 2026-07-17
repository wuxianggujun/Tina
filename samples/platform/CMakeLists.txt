add_executable(tina_sample_platform
    main.cpp
)

target_compile_features(tina_sample_platform PRIVATE cxx_std_23)
target_link_libraries(tina_sample_platform PRIVATE
    Tina::ProjectOptions
    Tina::Runtime
    Tina::PlatformGlfw
    Tina::Task
    Tina::Render
)
set_target_properties(tina_sample_platform PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
    add_custom_command(TARGET tina_sample_platform POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tina_sample_platform>
            $<TARGET_FILE_DIR:tina_sample_platform>
        COMMAND_EXPAND_LISTS
    )
endif()
