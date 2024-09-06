set(SUBMODULE_PROJECT_NAME "Engine")
set(SUBMODULE_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB_RECURSE ENGINE_FILES CONFIGURE_DEPENDS
        ${SUBMODULE_PROJECT_DIR}/src/*.c**
        ${SUBMODULE_PROJECT_DIR}/src/*.h**)

add_shader_compile_dir(${CMAKE_SOURCE_DIR}/resources/shaders "simple")
add_compile_options("$<$<CONFIG:DEBUG>:-DDEBUG>" "$<$<CONFIG:DEBUG>:-DENABLE_ASSERTS>")
add_library(${SUBMODULE_PROJECT_NAME} ${ENGINE_FILES})
GROUP_FILES_BY_DIRECTORY("ENGINE_FILES")

add_dependencies(${SUBMODULE_PROJECT_NAME} shaders)

target_link_libraries(${SUBMODULE_PROJECT_NAME} PUBLIC ${TINA_LIBRARIES})

#[[target_link_libraries(${SUBMODULE_PROJECT_NAME} PRIVATE
        -lglfw
        -lGL
        -lm
        -lXrandr
        -lXi
        -lX11
        -lXxf86vm
        -lpthread
        -ldl
        -lXinerama
        -lXcursor)]]

add_definitions(
    -DGLFW_INCLUDE_NONE
    -D_CRT_SECURE_NO_WARNINGS
    -DUNICODE
)

if(msvc)
  add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
  add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
  target_compile_options(${SUBMODULE_PROJECT_NAME} PRIVATE "/bigobj")
else()
  # add_compile_options(-Wa,-mbig-obj)
endif()

target_include_directories(${SUBMODULE_PROJECT_NAME} PUBLIC ${SUBMODULE_PROJECT_DIR}/src)
