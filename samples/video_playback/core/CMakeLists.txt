# The device probe plus the demuxer this sample shares with tina_sample_video_demux. The demuxer
# source lives in that sample's core/ and is compiled here as well rather than being promoted to
# an engine module: it is sample-grade code, and two samples referencing one file is honest about
# that where a second copy would not be.
tina_add_sample_core(tina_sample_video_playback_core
    SOURCES
        PlaybackDeviceProbe.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../video_demux/core/Mp4Demux.cpp
    INCLUDE
        ${PROJECT_SOURCE_DIR}/samples/video_demux/core
        ${PROJECT_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx/3rdparty
    LINK
        Tina::Core
        Tina::Render
        Tina::Runtime
        TinaBgfxRuntime::l-smash
)
