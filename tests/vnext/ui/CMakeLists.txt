add_executable(tina_ui_tests
    UIHitSnapshotTests.cpp
    UILayoutTests.cpp
    UITreeCoreTests.cpp
    header_isolation/InputRoutingHeader.cpp
    header_isolation/UIDirtyHeader.cpp
    header_isolation/UICommittedHitHeader.cpp
    header_isolation/UICommittedLayoutHeader.cpp
    header_isolation/UICommittedStructureHeader.cpp
    header_isolation/UIContextHeader.cpp
    header_isolation/UIErrorsHeader.cpp
    header_isolation/UIHitTestHeader.cpp
    header_isolation/UILayoutHeader.cpp
    header_isolation/UINodeIdHeader.cpp
    header_isolation/UIUmbrellaHeader.cpp
    header_isolation/UIWidgetKindHeader.cpp
)

target_compile_features(tina_ui_tests PRIVATE cxx_std_23)
target_link_libraries(tina_ui_tests
    PRIVATE
        Tina::ProjectOptions
        Tina::UI
        GTest::gtest_main
)
set_target_properties(tina_ui_tests PROPERTIES CXX_EXTENSIONS OFF)
