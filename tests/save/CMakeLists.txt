set(TINA_SAVE_HEADER_ISOLATION_SOURCES
    header_isolation/SaveErrorsHeader.cpp
    header_isolation/SaveMigrationHeader.cpp
    header_isolation/SaveStoreHeader.cpp
    header_isolation/SaveTypesHeader.cpp
)

add_library(tina_save_header_isolation OBJECT
    ${TINA_SAVE_HEADER_ISOLATION_SOURCES}
)
target_compile_features(tina_save_header_isolation PRIVATE cxx_std_23)
target_include_directories(tina_save_header_isolation PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(tina_save_header_isolation PRIVATE
    Tina::ProjectOptions
)
set_target_properties(tina_save_header_isolation PROPERTIES CXX_EXTENSIONS OFF)

add_executable(tina_save_tests
    SaveMigrationTests.cpp
    SaveStoreTests.cpp
    $<TARGET_OBJECTS:tina_save_header_isolation>
)
target_compile_features(tina_save_tests PRIVATE cxx_std_23)
target_link_libraries(tina_save_tests PRIVATE
    Tina::ProjectOptions
    Tina::Save
    Tina::Task
    GTest::gtest_main
)
set_target_properties(tina_save_tests PROPERTIES CXX_EXTENSIONS OFF)
