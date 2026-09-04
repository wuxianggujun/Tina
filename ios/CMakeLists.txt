# The UIKit host. Gated on IOS so a Windows configure never sees a .mm file it cannot compile.
#
# This target is the analogue of android/app: it owns CAMetalLayer, CADisplayLink, UITextInput and
# UITouch, and talks to IosSession. It does not yet own EngineHost -- that is a later slice that
# consumes the backend IosSession already builds. Until then CADisplayLink drives pollFrame().
if(NOT IOS)
    return()
endif()

add_executable(tina_ios_host MACOSX_BUNDLE
    app/main.mm
    app/TinaAppDelegate.h
    app/TinaAppDelegate.mm
    app/TinaViewController.h
    app/TinaViewController.mm
    app/TinaMetalView.h
    app/TinaMetalView.mm
)
add_executable(Tina::IosHost ALIAS tina_ios_host)

target_compile_features(tina_ios_host PRIVATE cxx_std_23)
target_include_directories(tina_ios_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_link_libraries(tina_ios_host PRIVATE
    Tina::PlatformIos
    Tina::WindowSurfaceIntegration
    "-framework UIKit"
    "-framework QuartzCore"
    "-framework Metal"
    "-framework Foundation"
)
set_target_properties(tina_ios_host PROPERTIES
    CXX_EXTENSIONS OFF
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/app/Info.plist
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER dev.tina.host
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_UILaunchScreen_Generation YES
)
