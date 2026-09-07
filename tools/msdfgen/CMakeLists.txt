# Host font preprocessor: exactly the same msdfgen/HarfBuzz implementation as
# runtime misses, not a second font renderer with different metrics or ranges.
find_package(PNG REQUIRED)
add_executable(tina_msdfgen main.cpp)
target_compile_features(tina_msdfgen PRIVATE cxx_std_23)
target_link_libraries(tina_msdfgen PRIVATE Tina::ProjectOptions Tina::UIFreetype PNG::PNG)
if(WIN32)
    target_link_libraries(tina_msdfgen PRIVATE shell32)
endif()
set_target_properties(tina_msdfgen PROPERTIES CXX_EXTENSIONS OFF)
