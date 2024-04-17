if(TINA_BUILD_TESTING)

set(TINA_TESE_TARGET_NAME Tina_Tests)
set(TEST_SRC_DIRS)
#aux_source_directory(opengl TEST_SRC_DIRS)
aux_source_directory(src TEST_SRC_DIRS)

add_executable(${TINA_TESE_TARGET_NAME} ${TEST_SRC_DIRS})
target_link_libraries(${TINA_TESE_TARGET_NAME}  PUBLIC  GTest::gtest_main)

add_test(NAME ${TINA_TESE_TARGET_NAME} COMMAND ${TINA_TESE_TARGET_NAME})

endif()
