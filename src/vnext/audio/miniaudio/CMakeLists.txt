# TINA_AUDIO_ENABLE_LIBVORBIS / TINA_AUDIO_ENABLE_LIBOPUS are root-level options.

set(TINA_AUDIO_MINIAUDIO_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/audio/AudioDecode.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/audio/miniaudio/MiniaudioDevice.hpp
)

set(TINA_AUDIO_MINIAUDIO_SOURCES
    ${TINA_AUDIO_MINIAUDIO_PUBLIC_HEADERS}
    MiniaudioDecode.cpp
    MiniaudioDevice.cpp
)

# Exactly one MINIAUDIO_IMPLEMENTATION TU per process image.
# Legacy already owns it via Tina::Miniaudio when TINA_BUILD_LEGACY=ON.
if (NOT TINA_BUILD_LEGACY)
    list(APPEND TINA_AUDIO_MINIAUDIO_SOURCES MiniaudioImplementation.cpp)
endif ()

add_library(tina_audio_miniaudio STATIC ${TINA_AUDIO_MINIAUDIO_SOURCES})
add_library(Tina::AudioMiniaudio ALIAS tina_audio_miniaudio)

target_compile_features(tina_audio_miniaudio PUBLIC cxx_std_23)
target_include_directories(tina_audio_miniaudio
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${TINA_MINIAUDIO_INCLUDE_DIR}
)
# Built-in WAV/FLAC/MP3 stay enabled. Optional codecs are opt-in so default
# graphs stay small and do not force libvorbis/libopus on every product.
if (NOT TINA_AUDIO_ENABLE_LIBVORBIS)
    target_compile_definitions(tina_audio_miniaudio PRIVATE MA_NO_LIBVORBIS)
else ()
    target_compile_definitions(tina_audio_miniaudio PRIVATE TINA_AUDIO_HAS_LIBVORBIS=1)
endif ()
if (NOT TINA_AUDIO_ENABLE_LIBOPUS)
    target_compile_definitions(tina_audio_miniaudio PRIVATE MA_NO_LIBOPUS)
else ()
    target_compile_definitions(tina_audio_miniaudio PRIVATE TINA_AUDIO_HAS_LIBOPUS=1)
endif ()

target_link_libraries(tina_audio_miniaudio
    PUBLIC Tina::Audio
    PRIVATE Tina::ProjectOptions
)
if (TINA_BUILD_LEGACY)
    target_link_libraries(tina_audio_miniaudio PRIVATE Tina::Miniaudio)
endif ()

if (TINA_AUDIO_ENABLE_LIBVORBIS)
    target_link_libraries(tina_audio_miniaudio PRIVATE Vorbis::vorbis Vorbis::vorbisfile)
endif ()
if (TINA_AUDIO_ENABLE_LIBOPUS)
    target_link_libraries(tina_audio_miniaudio PRIVATE Opus::opus Opusfile::opusfile)
endif ()

if (UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(tina_audio_miniaudio PRIVATE ${CMAKE_DL_LIBS} Threads::Threads m)
endif ()
set_target_properties(tina_audio_miniaudio PROPERTIES CXX_EXTENSIONS OFF)
