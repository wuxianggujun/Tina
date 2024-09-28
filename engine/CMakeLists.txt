set(SUBMODULE_PROJECT_NAME "Engine")
set(SUBMODULE_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB_RECURSE ENGINE_FILES CONFIGURE_DEPENDS
        ${SUBMODULE_PROJECT_DIR}/src/*.c**
        ${SUBMODULE_PROJECT_DIR}/src/*.h**
        ${SUBMODULE_PROJECT_DIR}/src/*.inl
)

if (UNIX)
    find_package(X11 QUIET REQUIRED)
    #find_package(Wayland QUIET REQUIRED)
    #find_package(EGL QUIET REQUIRED)
endif ()

find_package(OpenGL REQUIRED COMPONENTS EGL)
#find_package(Wayland REQUIRED COMPONENTS Client Egl)

add_shader_compile_dir(${CMAKE_SOURCE_DIR}/resources/shaders "bump")
add_compile_options("$<$<CONFIG:DEBUG>:-DDEBUG>" "$<$<CONFIG:DEBUG>:-DENABLE_ASSERTS>")
add_library(${SUBMODULE_PROJECT_NAME} ${ENGINE_FILES})
GROUP_FILES_BY_DIRECTORY("ENGINE_FILES")

add_dependencies(${SUBMODULE_PROJECT_NAME} shaders)

if (UNIX)
    set(UNIX_LIBS -ldl -lpthread -lwayland-client -lwayland-egl -lEGL -lX11 -lXrandr -lXinerama -lXi -lXxf86vm -lXcursor -lXrender -lXfixes -lXext -lXau -lXdmcp)
    target_link_libraries(${SUBMODULE_PROJECT_NAME} PUBLIC ${UNIX_LIBS})
endif ()

if (WAYLAND_FOUND)
    target_link_libraries(${SUBMODULE_PROJECT_NAME} INTERFACE OpenGL::OpenGL OpenGL::EGL)
endif ()

target_link_libraries(${SUBMODULE_PROJECT_NAME} PUBLIC ${OPENGL_LIBRARIES} ${TINA_LIBRARIES})

add_definitions(
        -DGLFW_INCLUDE_NONE
)

# Release build optimizations for MSVC.
if(MSVC)
    add_definitions("/D_CRT_SECURE_NO_WARNINGS")
    foreach(config CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
        # Set warning level 3 instead of 4.
        string(REPLACE "/W3" "/W4" ${config} "${${config}}")

        # Enable intrinsic functions, disable minimal rebuild, UTF-8 source, set __cplusplus version.
        set(${config} "${${config}} /Oi /Gm- /utf-8 /Zc:__cplusplus")
    endforeach()

    # RelWithDebInfo is set to Ob1 instead of Ob2.   
    string(REPLACE "/Ob1" "/Ob2" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
    string(REPLACE "/Ob1" "/Ob2" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

    # Disable incremental linking in RelWithDebInfo.
    string(REPLACE "/INCREMENTAL" "/INCREMENTAL:NO" CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO}")

    # COMDAT folding/remove unused functions.
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /OPT:REF /OPT:ICF")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /OPT:REF /OPT:ICF")
endif()
if (UNIX)
    target_include_directories(${SUBMODULE_PROJECT_NAME} SYSTEM PUBLIC ${X11_INCLUDE_DIR}  ${WAYLAND_CLIENT_INCLUDE_DIR}
            ${WAYLAND_EGL_INCLUDE_DIR}  ${EGL_INCLUDE_DIR})
endif ()


target_include_directories(${SUBMODULE_PROJECT_NAME} PUBLIC ${SUBMODULE_PROJECT_DIR}/src)

