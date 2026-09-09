tina_add_runtime_module(tina_navigation2d
    ${PROJECT_SOURCE_DIR}/include/tina/navigation2d/NavigationErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/navigation2d/NavigationGrid2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/navigation2d/NavigationPathfinder2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/navigation2d/NavigationPathSmoother2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/navigation2d/NavigationFlowField2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/navigation2d/NavigationPathFollower2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/navigation2d/NavigationAgent2D.hpp
    NavigationGrid2D.cpp
    NavigationPathfinder2D.cpp
    NavigationPathSmoother2D.cpp
    NavigationFlowField2D.cpp
    NavigationPathFollower2D.cpp
    NavigationAgent2D.cpp
    NavigationTraversal2D.hpp
)
add_library(Tina::Navigation2D ALIAS tina_navigation2d)

target_compile_features(tina_navigation2d PUBLIC cxx_std_23)
target_include_directories(tina_navigation2d PUBLIC ${PROJECT_SOURCE_DIR}/include)
# The shared indexed heap lives in src/navigation, so it is reached as
# "navigation/NavigationIndexHeap.hpp" rather than through a sibling relative path.
target_include_directories(tina_navigation2d PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(tina_navigation2d
    PUBLIC Tina::Core Tina::Math
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_navigation2d PROPERTIES CXX_EXTENSIONS OFF)
