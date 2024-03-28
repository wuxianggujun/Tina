set(TINA_TESE_TARGET_NAME Tina_Tests)
if(TINA_BUILD_TESTING)

aux_source_directory(opengl TEST_OPENGL_SRCS)

add_executable(${TINA_TESE_TARGET_NAME} ${TEST_OPENGL_SRCS})
target_link_libraries(${TINA_TESE_TARGET_NAME}  PUBLIC  GTest::gtest_main)

add_test(NAME ${TINA_TESE_TARGET_NAME} COMMAND ${TINA_TESE_TARGET_NAME})

endif()