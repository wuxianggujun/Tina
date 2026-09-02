add_executable(tina_sample_video_demux
    main.cpp
    Mp4Demux.cpp
    Mp4Demux.hpp
)

target_compile_features(tina_sample_video_demux PRIVATE cxx_std_23)
# Tina::Render is here for the VideoDecodeTextureDesc vocabulary the demuxer fills in,
# not to create a device: this program never opens a window and never draws.
target_link_libraries(tina_sample_video_demux PRIVATE
    Tina::ProjectOptions
    Tina::Core
    Tina::Render
    TinaBgfxRuntime::l-smash
)
# h264.h is a single-header bitstream parser; only avcC's SPS needs it, so it is
# included by path rather than promoted to a target.
target_include_directories(tina_sample_video_demux PRIVATE
    ${PROJECT_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx/3rdparty
)
set_target_properties(tina_sample_video_demux PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
    add_custom_command(TARGET tina_sample_video_demux POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tina_sample_video_demux>
            $<TARGET_FILE_DIR:tina_sample_video_demux>
        COMMAND_EXPAND_LISTS
    )
endif()
