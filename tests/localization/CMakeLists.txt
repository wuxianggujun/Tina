add_executable(tina_localization_tests
    LocalizationCatalogTests.cpp
    header_isolation/LocalizationCatalogHeader.cpp
    header_isolation/LocalizationErrorsHeader.cpp
    header_isolation/LocalizedTextHeader.cpp
)
target_compile_features(tina_localization_tests PRIVATE cxx_std_23)
target_include_directories(tina_localization_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(tina_localization_tests PRIVATE
    Tina::ProjectOptions
    Tina::Localization
    GTest::gtest_main
)
set_target_properties(tina_localization_tests PROPERTIES CXX_EXTENSIONS OFF)
