set(TINA_MATH_HEADER_ISOLATION_SOURCES
    header_isolation/ConstantsHeader.cpp
    header_isolation/FrustumHeader.cpp
    header_isolation/Geometry2DHeader.cpp
    header_isolation/Geometry3DHeader.cpp
    header_isolation/Mat4Header.cpp
    header_isolation/QuaternionHeader.cpp
    header_isolation/VecHeader.cpp
)

# Each public header gets its own translation unit that includes nothing else, so a
# header that only compiles because a sibling was included first fails here rather
# than in a consumer's build.
add_library(tina_math_header_isolation OBJECT
    ${TINA_MATH_HEADER_ISOLATION_SOURCES}
)
target_compile_features(tina_math_header_isolation PRIVATE cxx_std_23)
target_include_directories(tina_math_header_isolation PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(tina_math_header_isolation PRIVATE
    Tina::ProjectOptions
)
set_target_properties(tina_math_header_isolation PROPERTIES CXX_EXTENSIONS OFF)

add_executable(tina_math_tests
    FrustumTests.cpp
    Geometry2DTests.cpp
    Geometry3DTests.cpp
    Mat4Tests.cpp
    QuaternionTests.cpp
    VecTests.cpp
    $<TARGET_OBJECTS:tina_math_header_isolation>
)
target_compile_features(tina_math_tests PRIVATE cxx_std_23)
target_link_libraries(tina_math_tests PRIVATE
    Tina::ProjectOptions
    Tina::Math
    GTest::gtest_main
)
set_target_properties(tina_math_tests PROPERTIES CXX_EXTENSIONS OFF)
