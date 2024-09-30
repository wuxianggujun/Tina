# ##############################################################################
#
# cmake settings
#
# ##############################################################################

cmake_minimum_required(VERSION 3.20...3.28)
project(Tina VERSION 0.0.1 LANGUAGES CXX)
  
# Cache the root directory
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Root directory path")

option(TINA_BUILD_WAYLAND "Build Wayland" ON)

list(APPEND CMAKE_MODULE_PATH ${ROOT_DIR}/cmake)


include(CheckCXXCompilerFlag)
 
set(CMAKE_CXX_STANDARD 17)

# Import the tool cmake script
include(TinaTool)

add_subdirectory(dependencies)

add_executable(${PROJECT_NAME} runtime/src/main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE ${TINA_LIBRARIES})
