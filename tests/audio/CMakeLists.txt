set(TINA_AUDIO_HEADER_ISOLATION_SOURCES
    header_isolation/AudioEngineHeader.cpp
    header_isolation/AudioErrorsHeader.cpp
    header_isolation/AudioIdsHeader.cpp
    header_isolation/AudioTypesHeader.cpp
)

add_library(tina_audio_header_isolation OBJECT
    ${TINA_AUDIO_HEADER_ISOLATION_SOURCES}
)
target_compile_features(tina_audio_header_isolation PRIVATE cxx_std_23)
target_include_directories(tina_audio_header_isolation PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(tina_audio_header_isolation PRIVATE
    Tina::ProjectOptions
)
set_target_properties(tina_audio_header_isolation PROPERTIES CXX_EXTENSIONS OFF)

add_executable(tina_audio_tests
    AudioEngineTests.cpp
    $<TARGET_OBJECTS:tina_audio_header_isolation>
)
target_compile_features(tina_audio_tests PRIVATE cxx_std_23)
target_include_directories(tina_audio_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tests
)
target_link_libraries(tina_audio_tests PRIVATE
    Tina::ProjectOptions
    Tina::Audio
    GTest::gtest_main
)
set_target_properties(tina_audio_tests PROPERTIES CXX_EXTENSIONS OFF)
