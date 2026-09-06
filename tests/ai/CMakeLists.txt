add_executable(tina_ai_tests AITests.cpp)
target_compile_features(tina_ai_tests PRIVATE cxx_std_23)
target_link_libraries(tina_ai_tests PRIVATE Tina::ProjectOptions Tina::AI GTest::gtest_main)
set_target_properties(tina_ai_tests PROPERTIES CXX_EXTENSIONS OFF)
