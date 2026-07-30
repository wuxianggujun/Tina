set(TINA_AUDIO_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/audio/AudioClipView.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/audio/AudioEngine.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/audio/AudioErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/audio/AudioIds.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/audio/AudioTypes.hpp
)

add_library(tina_audio STATIC
    ${TINA_AUDIO_PUBLIC_HEADERS}
    AudioEngine.cpp
)
add_library(Tina::Audio ALIAS tina_audio)

target_compile_features(tina_audio PUBLIC cxx_std_23)
target_include_directories(tina_audio PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_audio
    PUBLIC Tina::Core
    PRIVATE Tina::ProjectOptions
)
set_target_properties(tina_audio PROPERTIES CXX_EXTENSIONS OFF)
