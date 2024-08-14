cmake_minimum_required(VERSION 3.17)
project(TinaTest)

add_compile_options($<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall>)

file(GLOB TEST_SRC_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

foreach (_test_file ${TEST_SRC_FILES})
    get_filename_component(_test_name ${_test_file} NAME_WE)
    set(_full_test_name "Tina${_test_name}")

    add_executable(${_full_test_name} ${_test_file})
    target_link_libraries(${_full_test_name} Engine GTest::gtest_main)
    
    add_test(NAME ${_full_test_name} COMMAND ${_full_test_name})
    set_tests_properties(${_full_test_name} PROPERTIES TIMEOUT 10)
endforeach ()

# 如果需要在构建后复制文件，设置自定义命令
add_custom_command(TARGET ${_full_test_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/test.txt
        ${CMAKE_CURRENT_BINARY_DIR})
