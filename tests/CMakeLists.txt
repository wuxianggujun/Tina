cmake_minimum_required(VERSION 3.17)
project(TinaTest)

add_compile_options($<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall>)

file(GLOB TEST_SRC_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

foreach (_test_file ${TEST_SRC_FILES})
	get_filename_component(_test_name ${_test_file} NAME_WE)

	# 使用临时变量在可执行文件名前添加 "TinaTest"
	set(_full_test_name "TinaTest${_test_name}")

	# 添加带有前缀的可执行文件
	add_executable(${_full_test_name} EXCLUDE_FROM_ALL ${_test_file})
	target_link_libraries(${_full_test_name} Engine GTest::gtest_main)
	add_test(NAME ${_full_test_name} COMMAND ${_full_test_name})
	set_tests_properties(${_full_test_name} PROPERTIES TIMEOUT 10)

	# 如果需要在构建后复制文件，更新目标名称
	set(_post_build_target_name ${_full_test_name})
endforeach ()

# 为所有测试设置默认属性，需要在 foreach 循环之后设置
set_property(TEST ${_full_test_name} PROPERTY SKIP_RETURN_CODE 77)

# 复制测试数据文件的自定义命令，需要在 foreach 循环之外
add_custom_command(TARGET ${_post_build_target_name} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
		${CMAKE_CURRENT_SOURCE_DIR}/test.txt
		${CMAKE_CURRENT_BINARY_DIR}
)