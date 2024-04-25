set(SUBMODULE_PROJECT_NAME "Engine")

file(GLOB_RECURSE ENGINE_FILES CONFIGURE_DEPENDS
        src/framework/*.c**
        src/log/*.c**)

add_library(${SUBMODULE_PROJECT_NAME} ${ENGINE_FILES})

GROUP_FILES_BYDIRECTORY("ENGINE_FILES")

target_link_libraries(${SUBMODULE_PROJECT_NAME} PRIVATE ${TINA_LIBRARIES})

add_definitions(
    -DGLFW_INCLUDE_NONE
    -D_CRT_SECURE_NO_WARNINGS
    -DUNICODE)
#target_compile_definitions(${PROJECT_NAME} PUBLIC GLFW_INCLUDE_NONE)

if(msvc)
  target_compile_options(${SUBMODULE_PROJECT_NAME} PRIVATE "/bigobj")
else()
  add_compile_options(-Wa,-mbig-obj)
endif()

target_include_directories(${SUBMODULE_PROJECT_NAME} PUBLIC src)
