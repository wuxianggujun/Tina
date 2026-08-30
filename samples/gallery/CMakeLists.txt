# The gallery is split into a library and a desktop executable, because Android links the same scenes.
#
# tina_sample_gallery holds the menu and the scenes and depends only on Tina::Runtime -- no GLFW, no
# desktop bootstrap. That is what lets the Android JNI bridge link it. A single executable target could
# not: eight of the existing samples are unbuildable for Android precisely because their content and their
# GLFW composition root live in one translation unit.
add_library(tina_sample_gallery STATIC
    GalleryActions.hpp
    GalleryActions.cpp
    GalleryScene.hpp
    GalleryMenu.cpp
    GalleryScenes.cpp
)
add_library(Tina::SampleGallery ALIAS tina_sample_gallery)

target_compile_features(tina_sample_gallery PUBLIC cxx_std_23)
target_include_directories(tina_sample_gallery PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tina_sample_gallery
    PUBLIC
        Tina::Runtime
    PRIVATE
        $<BUILD_INTERFACE:Tina::ProjectOptions>
)
set_target_properties(tina_sample_gallery PROPERTIES CXX_EXTENSIONS OFF)

# The desktop front-end. Only built when GLFW is available, like every other windowed sample.
if(TINA_BUILD_PLATFORM_GLFW AND TINA_BUILD_RENDER_BGFX)
    add_executable(tina_sample_gallery_desktop
        DesktopMain.cpp
    )

    target_compile_features(tina_sample_gallery_desktop PRIVATE cxx_std_23)
    target_link_libraries(tina_sample_gallery_desktop PRIVATE
        Tina::ProjectOptions
        Tina::DesktopBootstrap
        Tina::SampleGallery
    )
    set_target_properties(tina_sample_gallery_desktop PROPERTIES CXX_EXTENSIONS OFF)

    if(WIN32)
        add_custom_command(TARGET tina_sample_gallery_desktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:tina_sample_gallery_desktop>
                $<TARGET_FILE_DIR:tina_sample_gallery_desktop>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()
