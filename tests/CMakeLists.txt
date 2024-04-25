set(TINA_TESE_TARGET_NAME ${PROJECT_NAME}_tests)
set(TEST_SRC_DIRS)
#aux_source_directory(opengl TEST_SRC_DIRS)
aux_source_directory(src TEST_SRC_DIRS)

add_executable(${TINA_TESE_TARGET_NAME} ${TEST_SRC_DIRS})
target_link_libraries(${TINA_TESE_TARGET_NAME} PRIVATE Engine)
target_link_libraries(${TINA_TESE_TARGET_NAME}  PUBLIC  GTest::gtest_main)

add_test(NAME ${TINA_TESE_TARGET_NAME} COMMAND ${TINA_TESE_TARGET_NAME})
