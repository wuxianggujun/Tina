add_library(tina_navigation2d STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/navigation2d/NavigationErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/navigation2d/NavigationGrid2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/navigation2d/NavigationPathfinder2D.hpp
    NavigationGrid2D.cpp
    NavigationPathfinder2D.cpp
)
add_library(Tina::Navigation2D ALIAS tina_navigation2d)

target_compile_features(tina_navigation2d PUBLIC cxx_std_23)
target_include_directories(tina_navigation2d PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_navigation2d
    PUBLIC Tina::Core
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_navigation2d PROPERTIES CXX_EXTENSIONS OFF)
