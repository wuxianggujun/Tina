add_library(tina_test_main STATIC EXCLUDE_FROM_ALL TestMain.cpp)
target_link_libraries(tina_test_main PUBLIC GTest::gtest PRIVATE Tina::ProjectOptions)
# Internal test infrastructure only. Never part of the SDK/install graph.
set_target_properties(tina_test_main PROPERTIES FOLDER "Tests/Support")
