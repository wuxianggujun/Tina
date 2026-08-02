cmake_minimum_required(VERSION 3.25)
project(TinaSdkAudioMiniaudioConsumer LANGUAGES CXX)

find_package(Tina 0.0.1 CONFIG REQUIRED COMPONENTS AudioMiniaudio)

if(NOT Tina_VERSION VERSION_EQUAL "0.0.1")
    message(FATAL_ERROR "Expected Tina package version 0.0.1, found '${Tina_VERSION}'")
endif()
if(NOT Tina_AudioMiniaudio_FOUND OR NOT TARGET Tina::AudioMiniaudio)
    message(FATAL_ERROR "The Tina package does not provide Tina::AudioMiniaudio")
endif()
if(NOT "Tina::AudioMiniaudio" IN_LIST Tina_ADAPTER_TARGETS OR
   NOT "Tina::AudioMiniaudio" IN_LIST Tina_PACKAGE_TARGETS)
    message(FATAL_ERROR "The Tina package does not declare its AudioMiniaudio adapter target")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../sdk_consumer/VerifyInstalledTargets.cmake")
tina_verify_installed_targets(${Tina_PACKAGE_TARGETS})
tina_verify_backend_neutral_game_sdk()

foreach(tina_audio_miniaudio_header IN ITEMS
        "${Tina_INCLUDE_DIR}/tina/audio/AudioDecode.hpp"
        "${Tina_INCLUDE_DIR}/tina/audio/miniaudio/MiniaudioDevice.hpp")
    if(NOT EXISTS "${tina_audio_miniaudio_header}")
        message(FATAL_ERROR "AudioMiniaudio package header is missing: ${tina_audio_miniaudio_header}")
    endif()
endforeach()

add_executable(tina_sdk_audio_miniaudio_consumer main.cpp)
target_compile_features(tina_sdk_audio_miniaudio_consumer PRIVATE cxx_std_23)
target_link_libraries(tina_sdk_audio_miniaudio_consumer PRIVATE Tina::AudioMiniaudio)
set_target_properties(tina_sdk_audio_miniaudio_consumer PROPERTIES CXX_EXTENSIONS OFF)
