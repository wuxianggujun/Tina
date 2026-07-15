add_executable(tina_tests
    core/AssertReleaseCompileTests.cpp
    core/AssertTests.cpp
    core/EnumFlagsTests.cpp
    core/LegacyCompatibilityTests.cpp
    core/ResultTests.cpp
    core/ScopeExitTests.cpp
    core/TimeTests.cpp
    core/TypesTests.cpp
    core/TypesWindowsAfterTests.cpp
    core/TypesWindowsBeforeTests.cpp
)

set_source_files_properties(core/AssertReleaseCompileTests.cpp PROPERTIES COMPILE_DEFINITIONS NDEBUG)

target_compile_features(tina_tests PRIVATE cxx_std_20)
target_link_libraries(tina_tests PRIVATE
    Tina::ProjectOptions
    Tina::CoreLegacy
    GTest::gtest_main
)
set_target_properties(tina_tests PROPERTIES CXX_EXTENSIONS OFF)
