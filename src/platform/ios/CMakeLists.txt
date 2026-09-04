add_library(tina_platform_ios STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/platform/ios/IosInputBridge.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/platform/ios/IosPlatformFactory.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/platform/ios/IosSession.hpp
    IosCompositionSession.hpp
    IosCompositionSession.cpp
    IosInputBridge.cpp
    IosPlatform.cpp
    IosSession.cpp
)
add_library(Tina::PlatformIos ALIAS tina_platform_ios)

target_compile_features(tina_platform_ios PUBLIC cxx_std_23)
target_include_directories(tina_platform_ios
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/integration
)
# No UIKit, no QuartzCore, and no Apple SDK header: the host hands the CAMetalLayer across as an
# opaque integer and pushes events into shared rings, so this adapter has the same dependency
# footprint as Headless. That is also why it is built on every host -- the alternative is an
# adapter that only compiles on a machine that can never run its tests.
target_link_libraries(tina_platform_ios
    PUBLIC
        Tina::Platform
        Tina::WindowSurfaceIntegration
    PRIVATE
        $<BUILD_INTERFACE:Tina::ProjectOptions>
)
set_target_properties(tina_platform_ios PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME PlatformIos
)

# Tests hang off this target rather than tests/CMakeLists.txt: that file is shared with every other
# adapter, and gating them here is what lets a desktop host actually execute the contract instead of
# only compiling it under an iOS toolchain nobody here runs.
if(TINA_BUILD_TESTING)
    add_subdirectory("${PROJECT_SOURCE_DIR}/tests/platform_ios" "${CMAKE_CURRENT_BINARY_DIR}/platform_ios_tests")
endif()

# The UIKit host lives next to android/app, not in this adapter: the adapter must stay Apple-SDK-free
# so Windows can run its tests. IOS is the CMake compiler-id gate, not an option, for the same reason
# ANDROID gates the JNI library.
if(IOS)
    add_subdirectory("${PROJECT_SOURCE_DIR}/ios" "${CMAKE_CURRENT_BINARY_DIR}/ios_host")
endif()
