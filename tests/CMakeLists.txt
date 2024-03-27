add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/3rdparty/googletest)

add_executable(${CMAKE_PROJECT_NAME} example.cpp)
target_link_libraries(${CMAKE_PROJECT_NAME} gtest_main)