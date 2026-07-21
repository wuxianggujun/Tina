set(TINA_AUDIO_MINIAUDIO_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/audio/miniaudio/MiniaudioDevice.hpp
)

set(TINA_AUDIO_MINIAUDIO_SOURCES
    ${TINA_AUDIO_MINIAUDIO_PUBLIC_HEADERS}
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
target_compile_definitions(tina_audio_miniaudio PRIVATE MA_NO_LIBVORBIS MA_NO_LIBOPUS)
target_link_libraries(tina_audio_miniaudio
    PUBLIC Tina::Audio
    PRIVATE Tina::ProjectOptions
)
if (TINA_BUILD_LEGACY)
    target_link_libraries(tina_audio_miniaudio PRIVATE Tina::Miniaudio)
endif ()
if (UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(tina_audio_miniaudio PRIVATE ${CMAKE_DL_LIBS} Threads::Threads m)
endif ()
set_target_properties(tina_audio_miniaudio PROPERTIES CXX_EXTENSIONS OFF)
