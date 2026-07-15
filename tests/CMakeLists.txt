add_executable(tina_core_tests
    core/AssertReleaseCompileTests.cpp
    core/AssertTests.cpp
    core/EnumFlagsTests.cpp
    core/LegacyCompatibilityTests.cpp
    core/ResultTests.cpp
    core/ScopeExitTests.cpp
    core/TestMain.cpp
    core/TimeTests.cpp
    core/TypesTests.cpp
    core/TypesWindowsAfterTests.cpp
    core/TypesWindowsBeforeTests.cpp
)

set_source_files_properties(core/AssertReleaseCompileTests.cpp PROPERTIES COMPILE_DEFINITIONS NDEBUG)

target_compile_features(tina_core_tests PRIVATE cxx_std_23)
target_link_libraries(tina_core_tests PRIVATE Tina::ProjectOptions Tina::CoreLegacy)
set_target_properties(tina_core_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME Tina.Core COMMAND tina_core_tests)
