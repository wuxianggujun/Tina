tina_add_runtime_module(tina_text BitmapFont.cpp
    ${PROJECT_SOURCE_DIR}/include/tina/text/BitmapFont.hpp)
add_library(Tina::Text ALIAS tina_text)
target_compile_features(tina_text PUBLIC cxx_std_23)
target_include_directories(tina_text PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_text PUBLIC Tina::Core PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>")
set_target_properties(tina_text PROPERTIES CXX_EXTENSIONS OFF)
