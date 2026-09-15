tina_add_runtime_module(tina_serialization JsonArchive.cpp
    ${PROJECT_SOURCE_DIR}/include/tina/serialization/JsonArchive.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/serialization/ObjectTable.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/serialization/TypeRegistry.hpp)
add_library(Tina::Serialization ALIAS tina_serialization)
target_compile_features(tina_serialization PUBLIC cxx_std_23)
target_include_directories(tina_serialization PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_serialization PUBLIC Tina::Core PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>")
set_target_properties(tina_serialization PROPERTIES CXX_EXTENSIONS OFF)
