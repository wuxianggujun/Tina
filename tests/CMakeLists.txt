set(TINA_TEST_TARGET_NAME ${PROJECT_NAME}_tests)
set(TEST_SRC_DIRS)

aux_source_directory(src TEST_SRC_DIRS)

add_compile_options(/W4)

#add_executable(${TINA_TESE_TARGET_NAME} src/ByteBufferTest.cpp)
add_executable(${TINA_TEST_TARGET_NAME} ${TEST_SRC_DIRS})
  
add_test(NAME ${TINA_TEST_TARGET_NAME} COMMAND ${TINA_TEST_TARGET_NAME})
#add_test(NAME ${TINA_TESE_TARGET_NAME} COMMAND src/ByteBufferTest.cpp)

target_link_libraries(${TINA_TEST_TARGET_NAME} PRIVATE Engine)
target_link_libraries(${TINA_TEST_TARGET_NAME}  PUBLIC  GTest::gtest_main)

add_custom_command(TARGET ${TINA_TEST_TARGET_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/test.txt
    ${CMAKE_CURRENT_SOURCE_DIR}
)
