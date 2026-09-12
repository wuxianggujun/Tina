set(TINA_AUDIO_MINIAUDIO_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/audio/miniaudio/MiniaudioDevice.hpp
)

set(TINA_AUDIO_MINIAUDIO_SOURCES
    ${TINA_AUDIO_MINIAUDIO_PUBLIC_HEADERS}
    MiniaudioDevice.cpp
)

tina_add_runtime_module(tina_audio_miniaudio ${TINA_AUDIO_MINIAUDIO_SOURCES})
add_library(Tina::AudioMiniaudio ALIAS tina_audio_miniaudio)

target_compile_features(tina_audio_miniaudio PUBLIC cxx_std_23)
target_include_directories(tina_audio_miniaudio
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${TINA_MINIAUDIO_INCLUDE_DIR}
)
target_compile_definitions(tina_audio_miniaudio PRIVATE MA_NO_ENGINE MA_NO_NODE_GRAPH MA_NO_RESOURCE_MANAGER)

target_link_libraries(tina_audio_miniaudio
    PUBLIC Tina::Audio
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
if (UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(tina_audio_miniaudio PRIVATE ${CMAKE_DL_LIBS} Threads::Threads m)
endif ()
set_target_properties(tina_audio_miniaudio PROPERTIES CXX_EXTENSIONS OFF)
