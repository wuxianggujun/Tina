set(TINA_AUDIO_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/audio/AudioClipView.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/audio/AudioDecode.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/audio/AudioEngine.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/audio/AudioErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/audio/AudioIds.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/audio/AudioTypes.hpp
)

tina_add_runtime_module(tina_audio
    ${TINA_AUDIO_PUBLIC_HEADERS}
    AudioEngine.cpp
    decode/AudioDecode.cpp
    decode/OggValidation.cpp
    decode/OggValidation.hpp
    decode/MiniaudioImplementation.cpp
    ${PROJECT_SOURCE_DIR}/thirdparty/miniaudio_decoders/libvorbis/miniaudio_libvorbis.c
    ${PROJECT_SOURCE_DIR}/thirdparty/miniaudio_decoders/libvorbis/miniaudio_libvorbis.h
    ${PROJECT_SOURCE_DIR}/thirdparty/miniaudio_decoders/libopus/miniaudio_libopus.c
    ${PROJECT_SOURCE_DIR}/thirdparty/miniaudio_decoders/libopus/miniaudio_libopus.h
)
add_library(Tina::Audio ALIAS tina_audio)

target_compile_features(tina_audio PUBLIC cxx_std_23)
target_include_directories(tina_audio PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_include_directories(tina_audio PRIVATE
    ${TINA_MINIAUDIO_INCLUDE_DIR}
    ${PROJECT_SOURCE_DIR}/thirdparty/miniaudio_decoders)
# Tina owns its mixer and queues; miniaudio supplies codecs and optional devices,
# not a second engine, node graph, resource manager or worker pool.
target_compile_definitions(tina_audio PRIVATE MA_NO_ENGINE MA_NO_NODE_GRAPH MA_NO_RESOURCE_MANAGER)
if(NOT TINA_BUILD_AUDIO_MINIAUDIO)
    target_compile_definitions(tina_audio PRIVATE MA_NO_DEVICE_IO MA_NO_THREADING)
endif()
target_link_libraries(tina_audio
    PUBLIC Tina::Core
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>" Vorbis::vorbisfile Opus::opus OpusFile::opusfile
)
if(UNIX AND NOT APPLE)
    target_link_libraries(tina_audio PRIVATE m)
    if(TINA_BUILD_AUDIO_MINIAUDIO)
        find_package(Threads REQUIRED)
        target_link_libraries(tina_audio PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
    endif()
endif()
set_target_properties(tina_audio PROPERTIES CXX_EXTENSIONS OFF)
