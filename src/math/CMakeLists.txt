set(TINA_MATH_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/math/Constants.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/math/Frustum.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/math/Geometry2D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/math/Geometry3D.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/math/Mat4.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/math/Quaternion.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/math/Vec.hpp
)

# Header-only by construction, not by accident: every entry point is a pure
# function over value types, so there is nothing to place in a translation unit.
# Same shape as Tina::AssetTypes.
add_library(tina_math INTERFACE)
add_library(Tina::Math ALIAS tina_math)

target_sources(tina_math INTERFACE
    "$<BUILD_INTERFACE:${TINA_MATH_PUBLIC_HEADERS}>"
)
target_compile_features(tina_math INTERFACE cxx_std_23)
target_include_directories(tina_math INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_math INTERFACE Tina::Core)
