add_executable(tina_sample_video_probe
    main.cpp
)

target_compile_features(tina_sample_video_probe PRIVATE cxx_std_23)
target_link_libraries(tina_sample_video_probe PRIVATE
    Tina::ProjectOptions
    Tina::DesktopBootstrap
    Tina::Runtime
)
set_target_properties(tina_sample_video_probe PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
    add_custom_command(TARGET tina_sample_video_probe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tina_sample_video_probe>
            $<TARGET_FILE_DIR:tina_sample_video_probe>
        COMMAND_EXPAND_LISTS
    )
endif()
