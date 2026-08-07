add_library(tina_editor_app STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/editor_app/EditorApplication.hpp
    EditorApplication.cpp
    EditorFileDialog.cpp
    EditorFileDialog.hpp
    EditorSourceImportLaunchOptions.cpp
    EditorSourceImportLaunchOptions.hpp
    EditorSourceImportService.cpp
    EditorSourceImportService.hpp
)
add_library(Tina::EditorApp ALIAS tina_editor_app)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(tina_editor_app PRIVATE
        EditorFileDialogLinux.cpp
        EditorFileDialogLinux.hpp
    )

    if(TINA_BUILD_TESTING)
        add_executable(tina_editor_file_dialog_gate
            EditorFileDialogGateMain.cpp
        )
        target_compile_features(tina_editor_file_dialog_gate PRIVATE cxx_std_23)
        target_link_libraries(tina_editor_file_dialog_gate PRIVATE Tina::EditorApp)
        set_target_properties(tina_editor_file_dialog_gate PROPERTIES
            CXX_EXTENSIONS OFF
        )
    endif()
endif()

target_compile_features(tina_editor_app PUBLIC cxx_std_23)
target_include_directories(tina_editor_app
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
)
target_link_libraries(tina_editor_app
    PRIVATE
        Tina::DesktopBootstrap
        Tina::Asset
        Tina::Editor
        Tina::Runtime
        Tina::Scene
        "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)
if(WIN32)
    target_link_libraries(tina_editor_app PRIVATE ole32 shell32 uuid)
endif()
set_target_properties(tina_editor_app PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME EditorApp
)

add_executable(tina_editor_desktop
    main.cpp
)
add_executable(Tina::EditorDesktop ALIAS tina_editor_desktop)

target_compile_features(tina_editor_desktop PRIVATE cxx_std_23)
target_link_libraries(tina_editor_desktop PRIVATE
    Tina::EditorApp
)
set_target_properties(tina_editor_desktop PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME TinaEditor
)

if(WIN32)
    add_custom_command(TARGET tina_editor_desktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tina_editor_desktop>
            $<TARGET_FILE_DIR:tina_editor_desktop>
        COMMAND_EXPAND_LISTS
    )
endif()
