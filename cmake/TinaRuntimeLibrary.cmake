include_guard(GLOBAL)
include(GNUInstallDirs)

# Source modules are compilation boundaries, not independently shipped libraries.
# Each object enters Tina exactly once. Do not replace this with archive merging,
# unity builds or WHOLE_ARCHIVE: all three weaken unused-code elimination.
function(tina_add_runtime_module target)
    add_library(${target} OBJECT ${ARGN})
    set_target_properties(${target} PROPERTIES
        TINA_RUNTIME_MODULE TRUE
        UNITY_BUILD OFF
        POSITION_INDEPENDENT_CODE ON
    )
    set_property(GLOBAL APPEND PROPERTY TINA_RUNTIME_MODULES ${target})
endfunction()

function(tina_initialize_runtime_library)
    add_library(tina_game_sdk STATIC)
    add_library(Tina::GameSDK ALIAS tina_game_sdk)
    target_compile_features(tina_game_sdk PUBLIC cxx_std_23)
    target_include_directories(tina_game_sdk PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    )
    target_link_libraries(tina_game_sdk PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>")
    target_compile_options(tina_game_sdk PUBLIC "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    set_target_properties(tina_game_sdk PROPERTIES
        EXPORT_NAME GameSDK
        OUTPUT_NAME Tina
        CXX_EXTENSIONS OFF
        UNITY_BUILD OFF
        POSITION_INDEPENDENT_CODE ON
        ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib"
    )

    # Keep the normal optimiser and precise floating-point semantics. Packaging
    # does not justify fast-math, disabled validation, or lower-quality shaders.
    if(MSVC)
        target_compile_options(tina_project_options INTERFACE
            "$<$<CONFIG:Release>:/Gy>"
            "$<$<CONFIG:Release>:/Gw>"
            "$<$<CONFIG:Release>:/Zc:inline>"
        )
        target_link_options(tina_game_sdk INTERFACE
            "$<$<CONFIG:Release>:/OPT:REF>"
            "$<$<CONFIG:Release>:/OPT:ICF>"
            "$<$<CONFIG:Release>:/INCREMENTAL:NO>"
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(tina_project_options INTERFACE
            "$<$<CONFIG:Release>:-ffunction-sections>"
            "$<$<CONFIG:Release>:-fdata-sections>"
        )
        if(APPLE)
            target_link_options(tina_game_sdk INTERFACE "$<$<CONFIG:Release>:LINKER:-dead_strip>")
        else()
            target_link_options(tina_game_sdk INTERFACE "$<$<CONFIG:Release>:LINKER:--gc-sections>")
        endif()
    endif()
endfunction()

function(tina_finalize_runtime_library)
    get_property(modules GLOBAL PROPERTY TINA_RUNTIME_MODULES)
    if(NOT modules)
        message(FATAL_ERROR "Tina's runtime library has no source modules")
    endif()

    set(dependencies)
    set(definitions)
    set(options)
    set(link_options)
    set(fingerprint_files "${PROJECT_SOURCE_DIR}/CMakeLists.txt" "${PROJECT_SOURCE_DIR}/vcpkg.json")
    foreach(module IN LISTS modules)
        target_sources(tina_game_sdk PRIVATE "$<TARGET_OBJECTS:${module}>")
        get_target_property(module_directory ${module} SOURCE_DIR)
        list(APPEND fingerprint_files "${module_directory}/CMakeLists.txt")
        get_target_property(sources ${module} SOURCES)
        foreach(source IN LISTS sources)
            if(source MATCHES "\\$<")
                continue()
            endif()
            cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${module_directory}" NORMALIZE
                OUTPUT_VARIABLE source_path)
            cmake_path(IS_PREFIX PROJECT_BINARY_DIR "${source_path}" NORMALIZE generated_source)
            if(EXISTS "${source_path}" AND NOT generated_source)
                list(APPEND fingerprint_files "${source_path}")
            endif()
        endforeach()

        # A static library's implementation dependencies still participate in the
        # final link. Forward only those dependencies, never the module targets
        # (which would either duplicate their objects or leak the build graph).
        get_target_property(module_links ${module} LINK_LIBRARIES)
        foreach(dependency IN LISTS module_links)
            if(dependency MATCHES "-NOTFOUND$" OR dependency MATCHES "^\\$<BUILD_INTERFACE:")
                continue()
            endif()
            if(TARGET ${dependency})
                get_target_property(real_target ${dependency} ALIASED_TARGET)
                if(NOT real_target)
                    set(real_target "${dependency}")
                endif()
                get_target_property(internal_module ${real_target} TINA_RUNTIME_MODULE)
                if(internal_module OR real_target STREQUAL "tina_math" OR
                   real_target STREQUAL "tina_asset_types" OR real_target STREQUAL "tina_project_options")
                    continue()
                endif()
            endif()
            list(APPEND dependencies "${dependency}")
        endforeach()

        foreach(property IN ITEMS INTERFACE_COMPILE_DEFINITIONS INTERFACE_COMPILE_OPTIONS)
            get_target_property(values ${module} ${property})
            if(values)
                if(property STREQUAL "INTERFACE_COMPILE_DEFINITIONS")
                    list(APPEND definitions ${values})
                else()
                    list(APPEND options ${values})
                endif()
            endif()
        endforeach()
        foreach(property IN ITEMS LINK_OPTIONS INTERFACE_LINK_OPTIONS)
            get_target_property(values ${module} ${property})
            if(values)
                list(APPEND link_options ${values})
            endif()
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES dependencies)
    list(REMOVE_DUPLICATES definitions)
    list(REMOVE_DUPLICATES options)
    list(REMOVE_DUPLICATES link_options)
    target_link_libraries(tina_game_sdk PRIVATE ${dependencies})
    target_compile_definitions(tina_game_sdk PUBLIC ${definitions})
    target_compile_options(tina_game_sdk PUBLIC ${options})
    target_link_options(tina_game_sdk INTERFACE ${link_options})

    set(features GameSDK)
    foreach(pair IN ITEMS
            "Physics2D=tina_physics2d" "Physics3D=tina_physics3d"
            "PlatformGlfw=tina_platform_glfw" "RenderBgfx=tina_render_bgfx"
            "UIFreetype=tina_ui_freetype" "UIUia=tina_ui_uia"
            "AudioMiniaudio=tina_audio_miniaudio" "NetworkTls=tina_network_tls"
            "Desktop=tina_bootstrap_desktop" "Android=tina_bootstrap_android"
            "PlatformAndroid=tina_platform_android"
            "PlatformHtml5=tina_platform_html5" "PlatformIos=tina_platform_ios"
            "TraceTracy=tina_trace_tracy")
        string(REPLACE "=" ";" fields "${pair}")
        list(GET fields 0 feature)
        list(GET fields 1 feature_target)
        if(TARGET ${feature_target})
            list(APPEND features "${feature}")
        endif()
    endforeach()
    set(TINA_SDK_FEATURES "${features}" CACHE INTERNAL "Compiled Tina SDK capabilities" FORCE)

    # Fingerprint source content rather than timestamps or just HEAD: this SDK is
    # frequently produced from an intentionally dirty worktree. Paths in the hash
    # are repository-relative, so relocating an identical producer does not change it.
    file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/include/tina/*.hpp" "${PROJECT_SOURCE_DIR}/include/tina/*.h")
    file(GLOB_RECURSE implementation_inputs CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/src/*.hpp" "${PROJECT_SOURCE_DIR}/src/*.h"
        "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.c"
        "${PROJECT_SOURCE_DIR}/src/*.sh" "${PROJECT_SOURCE_DIR}/src/*.sc")
    file(GLOB package_inputs CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/cmake/*.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/*.in")
    list(APPEND fingerprint_files ${public_headers} ${implementation_inputs} ${package_inputs})
    list(REMOVE_DUPLICATES fingerprint_files)
    list(SORT fingerprint_files)
    set(fingerprint "${PROJECT_VERSION}|${CMAKE_CXX_COMPILER_ID}|${CMAKE_CXX_COMPILER_VERSION}|${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}|${CMAKE_SYSTEM_NAME}|${CMAKE_SYSTEM_PROCESSOR}|${CMAKE_SIZEOF_VOID_P}|${features}|${definitions}|${CMAKE_MSVC_RUNTIME_LIBRARY}|${CMAKE_CXX_FLAGS}|${CMAKE_CXX_FLAGS_RELEASE}|${CMAKE_CXX_FLAGS_DEBUG}|${TINA_ENABLE_SANITIZERS}")
    foreach(input IN LISTS fingerprint_files)
        file(SHA256 "${input}" digest)
        file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${input}")
        string(APPEND fingerprint "\n${relative}:${digest}")
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${fingerprint_files})
    string(SHA256 TINA_SDK_BUILD_ID "${fingerprint}")
    set(TINA_SDK_BUILD_ID "${TINA_SDK_BUILD_ID}" CACHE INTERNAL "Source/configuration fingerprint" FORCE)
    string(JOIN "," TINA_SDK_FEATURE_TEXT ${features})
    configure_file("${PROJECT_SOURCE_DIR}/cmake/TinaBuildInfo.cpp.in"
        "${PROJECT_BINARY_DIR}/generated/TinaBuildInfo.cpp" @ONLY)
    target_sources(tina_game_sdk PRIVATE "${PROJECT_BINARY_DIR}/generated/TinaBuildInfo.cpp")
    target_compile_definitions(tina_game_sdk PRIVATE "TINA_SDK_BUILD_CONFIGURATION=\"$<CONFIG>\"")
    set_property(TARGET tina_game_sdk PROPERTY TINA_RUNTIME_MODULES "${modules}")
    set_property(TARGET tina_game_sdk PROPERTY TINA_SDK_BUILD_ID "${TINA_SDK_BUILD_ID}")
endfunction()
