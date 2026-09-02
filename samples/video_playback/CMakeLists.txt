add_executable(tina_sample_video_playback
    main.cpp
    ../video_demux/Mp4Demux.cpp
)

target_compile_features(tina_sample_video_playback PRIVATE cxx_std_23)
target_include_directories(tina_sample_video_playback PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../video_demux
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/bgfx.cmake/bgfx/3rdparty
)
target_link_libraries(tina_sample_video_playback PRIVATE
    Tina::ProjectOptions
    Tina::DesktopBootstrap
    Tina::Core
    Tina::Render
    Tina::Runtime
    TinaBgfxRuntime::l-smash
)
set_target_properties(tina_sample_video_playback PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
    add_custom_command(TARGET tina_sample_video_playback POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tina_sample_video_playback>
            $<TARGET_FILE_DIR:tina_sample_video_playback>
        COMMAND_EXPAND_LISTS
    )
endif()
