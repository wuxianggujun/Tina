add_library(tina_platform_html5 STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/platform/html5/Html5PlatformFactory.hpp
    Html5KeyTranslation.hpp
    Html5KeyTranslation.cpp
    Html5TouchSlotTable.hpp
    Html5TouchSlotTable.cpp
    Html5Platform.cpp
)
add_library(Tina::PlatformHtml5 ALIAS tina_platform_html5)

target_compile_features(tina_platform_html5 PUBLIC cxx_std_23)
target_include_directories(tina_platform_html5
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/integration
)
target_link_libraries(tina_platform_html5
    PUBLIC
        Tina::Platform
        Tina::WindowSurfaceIntegration
    PRIVATE
        $<BUILD_INTERFACE:Tina::ProjectOptions>
)
set_target_properties(tina_platform_html5 PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME PlatformHtml5
)

# Emscripten's html5.h event callbacks live in the core runtime, so this target needs
# no extra link option. The main-loop driver and asset packaging belong to the
# executable that embeds this backend, not to the backend itself.
