cmake_minimum_required(VERSION 3.25)
project(TinaSdkConsumer LANGUAGES CXX)
find_package(Tina 0.2.0 EXACT CONFIG REQUIRED COMPONENTS GameSDK)

include("${CMAKE_CURRENT_LIST_DIR}/VerifyInstalledTargets.cmake")
tina_verify_installed_targets(${Tina_PACKAGE_TARGETS})
tina_verify_single_library_sdk()
if("RenderBgfx" IN_LIST Tina_FEATURES)
    tina_verify_installed_shader_include_dirs(TRUE)
else()
    tina_verify_installed_shader_include_dirs(FALSE)
endif()

add_executable(tina_sdk_consumer main.cpp)
target_compile_features(tina_sdk_consumer PRIVATE cxx_std_23)
target_link_libraries(tina_sdk_consumer PRIVATE Tina::GameSDK)
set_target_properties(tina_sdk_consumer PROPERTIES CXX_EXTENSIONS OFF)

# This tiny real consumer proves that selecting the complete archive does not
# force all renderer, UI, physics and cooker code into the executable.
add_executable(tina_sdk_archive_probe BuildInfoProbe.cpp)
target_link_libraries(tina_sdk_archive_probe PRIVATE Tina::GameSDK)
target_compile_definitions(tina_sdk_archive_probe PRIVATE
    "TINA_EXPECTED_SDK_VERSION=\"${Tina_VERSION}\""
    "TINA_EXPECTED_SDK_BUILD_ID=\"${Tina_BUILD_ID}\"")
if(WIN32)
    foreach(consumer tina_sdk_consumer tina_sdk_archive_probe)
        add_custom_command(TARGET ${consumer} POST_BUILD
            COMMAND "$<$<BOOL:$<TARGET_RUNTIME_DLLS:${consumer}>>:${CMAKE_COMMAND}>"
                "$<$<BOOL:$<TARGET_RUNTIME_DLLS:${consumer}>>:-E;copy_if_different;$<TARGET_RUNTIME_DLLS:${consumer}>;$<TARGET_FILE_DIR:${consumer}>>"
            COMMAND_EXPAND_LISTS VERBATIM)
    endforeach()
endif()
