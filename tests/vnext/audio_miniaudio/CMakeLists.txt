set(TINA_AUDIO_MINIAUDIO_HEADER_ISOLATION_SOURCES
    header_isolation/AudioDecodeHeader.cpp
    header_isolation/MiniaudioDeviceHeader.cpp
)

add_library(tina_audio_miniaudio_header_isolation OBJECT
    ${TINA_AUDIO_MINIAUDIO_HEADER_ISOLATION_SOURCES}
)
target_compile_features(tina_audio_miniaudio_header_isolation PRIVATE cxx_std_23)
target_include_directories(tina_audio_miniaudio_header_isolation PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(tina_audio_miniaudio_header_isolation PRIVATE
    Tina::ProjectOptions
)
set_target_properties(tina_audio_miniaudio_header_isolation PROPERTIES CXX_EXTENSIONS OFF)

add_executable(tina_audio_miniaudio_tests
    MiniaudioDecodeTests.cpp
    MiniaudioDeviceTests.cpp
    $<TARGET_OBJECTS:tina_audio_miniaudio_header_isolation>
)
target_compile_features(tina_audio_miniaudio_tests PRIVATE cxx_std_23)
target_link_libraries(tina_audio_miniaudio_tests PRIVATE
    Tina::ProjectOptions
    Tina::AudioMiniaudio
    GTest::gtest_main
)
set_target_properties(tina_audio_miniaudio_tests PROPERTIES CXX_EXTENSIONS OFF)
