add_library(tina_editor_app STATIC
    ${PROJECT_SOURCE_DIR}/include/tina/editor_app/EditorApplication.hpp
    EditorAnimationPreview.cpp
    EditorAnimationPreview.hpp
    EditorApplication.cpp
    EditorWorkspaceState.hpp
    EditorWorkspaceAnimation.cpp
    EditorWorkspaceCommands.cpp
    EditorWorkspaceNodeProperties.cpp
    EditorWorkspaceDocuments.cpp
    EditorWorkspaceGizmo.cpp
    EditorWorkspaceHierarchy.cpp
    EditorWorkspaceInspector.cpp
    EditorWorkspaceLayoutDebugger.cpp
    EditorWorkspaceLifecycle.cpp
    EditorWorkspacePreview.cpp
    EditorWorkspaceProject.cpp
    EditorWorkspaceSourceImport.cpp
    EditorWorkspaceSpriteAssetPicker.cpp
    EditorWorkspaceStatus.cpp
    EditorWorkspaceTileMap.cpp
    EditorWorkspaceUiBuild.cpp
    EditorWorkspaceUiRecipes.cpp
    EditorWorkspaceUiRecipes.hpp
    EditorWorkspaceViewport.cpp
    EditorFileDialog.cpp
    EditorFileDialog.hpp
    EditorCompositeImageResolver.cpp
    EditorCompositeImageResolver.hpp
    EditorIconAtlas.hpp
    EditorIconAtlas.generated.hpp
    EditorIconAtlas.generated.inc
    EditorIconResources.cpp
    EditorIconResources.hpp
    EditorSourceImportLaunchOptions.cpp
    EditorSourceImportLaunchOptions.hpp
    EditorSourceImportIngress.cpp
    EditorSourceImportIngress.hpp
    EditorSourceImportLimits.hpp
    EditorSourceFileRename.cpp
    EditorSourceFileRename.hpp
    EditorSourceImportSelection.cpp
    EditorSourceImportSelection.hpp
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
    PRIVATE
        # Reaches src/core/io/PathUtil.hpp; see the note in src/asset/CMakeLists.txt.
        "${PROJECT_SOURCE_DIR}/src"
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

if(TINA_BUILD_UI_FREETYPE)
    tina_target_stage_ui_font(tina_editor_desktop)
endif()

if(WIN32)
    set_target_properties(tina_editor_desktop PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    target_link_libraries(tina_editor_desktop PRIVATE shell32)
endif()

# After OUTPUT_NAME above, which decides the installed directory name (TinaEditor).
# Also stages the runtime DLLs into the build tree, which is why the WIN32 block no
# longer copies them itself.
tina_install_product(tina_editor_desktop)
