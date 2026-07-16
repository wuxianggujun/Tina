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
    engine/EventSystemTests.cpp
    engine/ResourceTests.cpp
    ui/UIRuntimeTests.cpp
    ${PROJECT_SOURCE_DIR}/src/engine/EventSystem.cpp
    ${PROJECT_SOURCE_DIR}/src/engine/SubscriptionToken.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/UINode.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/UILayoutManager.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/UIStyle.cpp
)

set_source_files_properties(core/AssertReleaseCompileTests.cpp PROPERTIES COMPILE_DEFINITIONS NDEBUG)

target_compile_features(tina_tests PRIVATE cxx_std_20)
target_link_libraries(tina_tests PRIVATE
    Tina::ProjectOptions
    Tina::CoreLegacy
    GTest::gtest_main
    spdlog::spdlog
)
set_target_properties(tina_tests PROPERTIES CXX_EXTENSIONS OFF)

# Visual Studio is a multi-config generator. Keeping Debug and Release test
# executables in the same directory lets vcpkg's app-local deployment leave a
# same-timestamp GTest DLL from the other configuration in place, which mixes
# incompatible MSVC iterator-debug/CRT ABIs before main(). Isolate only the
# test runtime outputs; Tina's resource working directory remains unchanged.
if(CMAKE_CONFIGURATION_TYPES)
    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${config}" config_upper)
        set_target_properties(tina_tests PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${BIN_DIR}/${config}"
        )
    endforeach()
endif()
