add_library(tina_miniaudio STATIC MiniaudioImplementation.cpp)
add_library(Tina::Miniaudio ALIAS tina_miniaudio)

target_compile_features(tina_miniaudio PRIVATE cxx_std_20)
target_include_directories(tina_miniaudio PUBLIC ${TINA_MINIAUDIO_INCLUDE_DIR})
target_compile_definitions(tina_miniaudio PUBLIC MA_NO_LIBVORBIS MA_NO_LIBOPUS)
target_link_libraries(tina_miniaudio PRIVATE Tina::ProjectOptions)
set_target_properties(tina_miniaudio PROPERTIES CXX_EXTENSIONS OFF)

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(tina_miniaudio PUBLIC ${CMAKE_DL_LIBS} Threads::Threads m)
endif()
