tina_add_runtime_module(tina_navigation3d
    ${PROJECT_SOURCE_DIR}/include/tina/navigation3d/NavigationErrors3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/navigation3d/NavigationVolume3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/navigation3d/NavigationPathfinder3D.hpp
    NavigationVolume3DData.cpp
    NavigationVolume3D.cpp
    NavigationPathfinder3D.cpp
    NavigationTraversal3D.hpp
)
add_library(Tina::Navigation3D ALIAS tina_navigation3d)

target_compile_features(tina_navigation3d PUBLIC cxx_std_23)
target_include_directories(tina_navigation3d PUBLIC ${PROJECT_SOURCE_DIR}/include)
# The indexed heap in src/navigation is shared with Tina::Navigation2D and reached as
# "navigation/NavigationIndexHeap.hpp". Neither navigation module depends on the other.
target_include_directories(tina_navigation3d PRIVATE ${PROJECT_SOURCE_DIR}/src)
# Core and Math only, like Tina::Navigation2D. A cooked-payload bridge, if one is ever
# needed, belongs on the Asset side depending downward onto this module.
target_link_libraries(tina_navigation3d
    PUBLIC Tina::Core Tina::Math
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
set_target_properties(tina_navigation3d PROPERTIES CXX_EXTENSIONS OFF)
