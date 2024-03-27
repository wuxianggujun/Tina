if(TINA_BUILD_TESTING)

aux_source_directory(opengl TEST_OPENGL_SRCS)

add_executable(tests ${TEST_OPENGL_SRCS})
target_link_libraries(tests  PUBLIC  GTest::gtest_main)

add_test(NAME tests COMMAND tests)

endif()