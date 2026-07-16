set(TINA_CORE_TEST_SOURCES
    core/AssertReleaseCompileTests.cpp
    core/AssertTests.cpp
    core/EnumFlagsTests.cpp
    core/ResultTests.cpp
    core/ScopeExitTests.cpp
    core/TimeTests.cpp
    core/TypesTests.cpp
    core/TypesWindowsAfterTests.cpp
    core/TypesWindowsBeforeTests.cpp
)

add_executable(tina_tests ${TINA_CORE_TEST_SOURCES})

set_source_files_properties(
    core/AssertReleaseCompileTests.cpp
    PROPERTIES COMPILE_DEFINITIONS NDEBUG
)

target_compile_features(tina_tests PRIVATE cxx_std_23)
target_link_libraries(tina_tests PRIVATE
    Tina::ProjectOptions
    Tina::Core
    GTest::gtest_main
)
set_target_properties(tina_tests PROPERTIES CXX_EXTENSIONS OFF)

if(TINA_BUILD_LEGACY)
    target_sources(tina_tests PRIVATE
        core/LegacyCompatibilityTests.cpp
        engine/Camera3DTests.cpp
        engine/EventSystemTests.cpp
        engine/ResourceTests.cpp
        ui/UIActionTests.cpp
        ui/UIRuntimeTests.cpp
        ${PROJECT_SOURCE_DIR}/src/engine/EventSystem.cpp
        ${PROJECT_SOURCE_DIR}/src/engine/SubscriptionToken.cpp
        ${PROJECT_SOURCE_DIR}/src/ui/UINode.cpp
        ${PROJECT_SOURCE_DIR}/src/ui/UILayoutManager.cpp
        ${PROJECT_SOURCE_DIR}/src/ui/UIStyle.cpp
        ${PROJECT_SOURCE_DIR}/src/ui/UIScrollView.cpp
    )
    target_link_libraries(tina_tests PRIVATE
        Tina::CoreLegacy
        spdlog::spdlog
        bx
    )
endif()
