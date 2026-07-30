set(TINA_UI_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UI.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIBehavior.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIButton.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIDropdown.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIListView.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UITreeView.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIProgressBar.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIPopup.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIRadioButton.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIScrollView.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UISlider.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIStyle.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIDirty.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UICommittedHit.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UICommittedLayout.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UICommittedPaint.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UICommittedStructure.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIContent.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UISemantics.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIAccessibility.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIContext.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIContextConfig.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIElement.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIEventRouting.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIHitTest.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UILayout.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIPaint.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UITheme.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIText.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UITextEdit.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/text/UIGlyphAtlas.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/text/UITextRasterizer.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/InputRouting.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UINodeId.hpp
)

add_library(tina_ui STATIC
    ${TINA_UI_PUBLIC_HEADERS}
    UIContext.cpp
    UIContextConfig.cpp
    detail/UIButtonActionRegistry.hpp
    detail/UIButtonActionRegistry.cpp
    detail/UIImeCompositionState.hpp
    detail/UIImeCompositionState.cpp
    detail/UISliderChangeCallbackRegistry.hpp
    detail/UISliderChangeCallbackRegistry.cpp
    detail/UITextStorage.hpp
    detail/UITextStorage.cpp
    UIText.cpp
    UIGlyphAtlas.cpp
    UITextRasterizer.cpp
)
add_library(Tina::UI ALIAS tina_ui)

target_compile_features(tina_ui PUBLIC cxx_std_23)
target_include_directories(tina_ui PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_ui
    PUBLIC
        Tina::Core
        Tina::Platform
    PRIVATE
        Tina::ProjectOptions
)
set_target_properties(tina_ui PROPERTIES CXX_EXTENSIONS OFF)

if(TINA_BUILD_UI_FREETYPE)
    add_library(tina_ui_freetype STATIC
        ${PROJECT_SOURCE_DIR}/include/tina/ui/text/FreeTypeTextRasterizerFactory.hpp
        freetype/FreeTypeTextRasterizer.cpp
    )
    add_library(Tina::UIFreetype ALIAS tina_ui_freetype)
    target_compile_features(tina_ui_freetype PUBLIC cxx_std_23)
    target_include_directories(tina_ui_freetype PUBLIC ${PROJECT_SOURCE_DIR}/include)
    target_link_libraries(tina_ui_freetype
        PUBLIC
            Tina::UI
        PRIVATE
            Tina::ProjectOptions
            Freetype::Freetype
    )
    set_target_properties(tina_ui_freetype PROPERTIES
        CXX_EXTENSIONS OFF
        EXPORT_NAME UIFreetype
    )
endif()

# Optional Windows UIA private adapter (COM / UIAutomation stay out of Game SDK headers).
if(TINA_BUILD_UI_UIA)
    if(NOT WIN32)
        message(FATAL_ERROR "TINA_BUILD_UI_UIA is Windows-only.")
    endif()
    add_library(tina_ui_uia STATIC
        ${PROJECT_SOURCE_DIR}/include/tina/ui/WindowsUiaAccessibilityProviderFactory.hpp
        uia/UIUiaMapping.hpp
        uia/WindowsUiaAccessibilityProvider.hpp
        uia/WindowsUiaAccessibilityProvider.cpp
        uia/WindowsUiaActionDispatch.hpp
        uia/WindowsUiaComProviders.hpp
        uia/WindowsUiaComProviders.cpp
        uia/WindowsUiaHostBridge.hpp
        uia/WindowsUiaHostBridge.cpp
    )
    add_library(Tina::UIUia ALIAS tina_ui_uia)
    target_compile_features(tina_ui_uia PUBLIC cxx_std_23)
    target_include_directories(tina_ui_uia
        PUBLIC
            ${PROJECT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/uia
    )
    target_link_libraries(tina_ui_uia
        PUBLIC
            Tina::UI
        PRIVATE
            Tina::ProjectOptions
            ole32
            oleaut32
            oleacc
            UIAutomationCore
            comctl32
    )
    set_target_properties(tina_ui_uia PROPERTIES
        CXX_EXTENSIONS OFF
        EXPORT_NAME UIUia
    )
endif()
