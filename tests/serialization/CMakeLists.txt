add_executable(tina_serialization_tests
    JsonArchiveTests.cpp
    TypeRegistryTests.cpp
    header_isolation/JsonArchiveHeader.cpp
    header_isolation/TypeRegistryHeader.cpp
    header_isolation/ObjectTableHeader.cpp
)
target_compile_features(tina_serialization_tests PRIVATE cxx_std_23)
target_include_directories(tina_serialization_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(tina_serialization_tests PRIVATE Tina::ProjectOptions Tina::GameSDK tina_test_main)
set_target_properties(tina_serialization_tests PROPERTIES CXX_EXTENSIONS OFF)
