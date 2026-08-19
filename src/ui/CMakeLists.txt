set(TINA_UI_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UI.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIBadge.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIBehavior.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIButton.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UICheckbox.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIDialog.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIDivider.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIDropdown.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIListView.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIMenu.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UITreeView.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIProgressBar.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIPopup.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIRadioButton.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIRangeInput.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIScrollView.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UISlider.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UISplitView.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UITabView.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIStyle.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UISurface.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIMotion.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIDirty.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UICommittedHit.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UICommittedLayout.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UICommittedPaint.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UICommittedStructure.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIComponentBuild.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIContent.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UISemantics.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIAccessibility.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIContext.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIContextConfig.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIFlow.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIFormField.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIElement.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIEventRouting.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIFocus.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIHitTest.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIIcon.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIIconButton.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIImage.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIImageSource.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UILayout.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIPaint.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UITheme.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIText.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UITextEdit.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UIToggleSwitch.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UITooltip.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/text/UIGlyphAtlas.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/text/UITextRasterizer.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/InputRouting.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/ui/UINodeId.hpp
)

add_library(tina_ui STATIC
    ${TINA_UI_PUBLIC_HEADERS}
    UIContext.cpp
    UIContextConfig.cpp
    detail/UIComponentRecipes.cpp
    detail/UIBehaviorStateStorage.hpp
    detail/UIBehaviorStateStorage.cpp
    detail/UIButtonActionRegistry.hpp
    detail/UIButtonActionRegistry.cpp
    detail/UICanvasCommandStorage.hpp
    detail/UICanvasCommandStorage.cpp
    detail/UIImageContentStorage.hpp
    detail/UIImageContentStorage.cpp
    detail/UIKeyframeTimelineStorage.hpp
    detail/UIKeyframeTimelineStorage.cpp
    detail/UIMenuInput.hpp
    detail/UIMenuLayout.hpp
    detail/UIMenuStateStorage.hpp
    detail/UIMenuStateStorage.cpp
    detail/UIMotionTrackStorage.hpp
    detail/UIMotionTrackStorage.cpp
    detail/UICommandPressLatch.hpp
    detail/UIControlGeometry.hpp
    detail/UIControlGeometry.cpp
    detail/UIControlPaintEmitter.hpp
    detail/UIControlPaintEmitter.cpp
    detail/UIControlValuePrimitives.hpp
    detail/UIContextLifetimeControl.hpp
    detail/UIContextLifetimeControl.cpp
    detail/UIElementContractResolver.hpp
    detail/UIElementContractResolver.cpp
    detail/UIFlexLayout.hpp
    detail/UIDefaultActionPressState.hpp
    detail/UIDefaultActionPressState.cpp
    detail/UIDirtyQueueStorage.hpp
    detail/UIDirtyQueueStorage.cpp
    detail/UIImeCompositionState.hpp
    detail/UIImeCompositionState.cpp
    detail/UINineSlicePaintEmitter.hpp
    detail/UINineSlicePaintEmitter.cpp
    detail/UIInputPrimitives.hpp
    detail/UIPointerRouteInspection.hpp
    detail/UIPointerRoutePath.hpp
    detail/UILayoutMeasurement.hpp
    detail/UILayoutPrimitives.hpp
    detail/UIPropertyNormalization.hpp
    detail/UIPropertyNormalization.cpp
    detail/UIRangeInputPressLatch.hpp
    detail/UIRangeInputPressLatch.cpp
    detail/UIPaintPrimitives.hpp
    detail/UIPaintPrimitives.cpp
    detail/UIPaintSnapshotBuilder.hpp
    detail/UIPaintSnapshotBuilder.cpp
    detail/UIRoutedPointerListenerRegistry.hpp
    detail/UIRoutedPointerListenerRegistry.cpp
    detail/UIScrollViewLayout.hpp
    detail/UISliderChangeCallbackRegistry.hpp
    detail/UISliderChangeCallbackRegistry.cpp
    detail/UISplitViewInput.hpp
    detail/UISplitViewLayout.hpp
    detail/UISplitViewStateStorage.hpp
    detail/UISplitViewStateStorage.cpp
    detail/UITabViewInput.hpp
    detail/UITabViewLayout.hpp
    detail/UITabViewStateStorage.hpp
    detail/UITabViewStateStorage.cpp
    detail/UISemanticsSnapshotBuilder.hpp
    detail/UISemanticsSnapshotBuilder.cpp
    detail/UIStyleRoleResolver.hpp
    detail/UIStyleRoleResolver.cpp
    detail/UIStyleSheetStorage.hpp
    detail/UIStyleSheetStorage.cpp
    detail/UIThemeTransitionResolver.hpp
    detail/UIThemeTransitionResolver.cpp
    detail/UIToggleSwitchGeometry.hpp
    detail/UITooltipLayout.hpp
    detail/UITooltipStateStorage.hpp
    detail/UITooltipStateStorage.cpp
    detail/UIVirtualCollectionLayout.hpp
    detail/UITextStorage.hpp
    detail/UITextStorage.cpp
    detail/UIGraphemeBreak.hpp
    detail/UIGraphemeBreak.cpp
    detail/UIGraphemeBreakData.inc
    detail/UITextEditModel.hpp
    detail/UITextEditModel.cpp
    detail/UITextEditPaintEmitter.hpp
    detail/UITextEditPaintEmitter.cpp
    detail/UITextPaintEmitter.hpp
    detail/UITextPaintEmitter.cpp
    detail/UITextTruncation.hpp
    detail/UITextTruncation.cpp
    detail/UIWidgetTraits.hpp
    detail/UIWidgetStateModels.hpp
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
        "$<BUILD_INTERFACE:Tina::ProjectOptions>"
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
            "$<BUILD_INTERFACE:Tina::ProjectOptions>"
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
            "$<BUILD_INTERFACE:Tina::ProjectOptions>"
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
