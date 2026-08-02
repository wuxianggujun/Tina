#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/platform/Window.hpp>
#include <tina/ui/UIAccessibility.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UICheckbox.hpp>
#include <tina/ui/UICommittedHit.hpp>
#include <tina/ui/UICommittedLayout.hpp>
#include <tina/ui/UICommittedPaint.hpp>
#include <tina/ui/UICommittedStructure.hpp>
#include <tina/ui/UIComponentBuild.hpp>
#include <tina/ui/UIContent.hpp>
#include <tina/ui/UIContextConfig.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UIDropdown.hpp>
#include <tina/ui/UIErrors.hpp>
#include <tina/ui/UIEventRouting.hpp>
#include <tina/ui/UIFocus.hpp>
#include <tina/ui/UIHitTest.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIPopup.hpp>
#include <tina/ui/UIProgressBar.hpp>
#include <tina/ui/UIRadioButton.hpp>
#include <tina/ui/UIRangeInput.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UISemantics.hpp>
#include <tina/ui/UISlider.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITextEdit.hpp>
#include <tina/ui/UITheme.hpp>
#include <tina/ui/UITreeView.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>

// Platform text composition stage lives in platform Input.hpp (already included
// transitively via UIEventRouting / PlatformFrame).

namespace Tina::UI::Detail {

struct UIContextLifetimeControl;

} // namespace Tina::UI::Detail

namespace Tina::UI {

struct UIStyleStatistics final {
    usize classCapacity = 0;
    usize registeredClassCount = 0;
    usize classHighWater = 0;
    usize ruleCapacity = 0;
    usize activeRuleCount = 0;
    usize ruleHighWater = 0;
    usize bucketCapacity = 0;
    usize activeBucketCount = 0;
    usize bucketHighWater = 0;
    usize rulesPerBucketCapacity = 0;
    usize bucketCandidateHighWater = 0;
    usize nodeClassLinkCapacity = 0;
    usize activeNodeClassLinkCount = 0;
    usize nodeClassLinkHighWater = 0;
    usize compileFailureCount = 0;
    usize capacityFailureCount = 0;
    u64 revision = 0;
};

struct UIContextStatistics final {
    usize nodeCapacity = 0;
    usize rootCapacity = 0;
    usize dirtyQueueCapacity = 0;
    usize layoutSnapshotCapacity = 0;
    usize hitSnapshotCapacity = 0;
    usize paintSnapshotCapacity = 0;
    usize canvasCommandCapacity = 0;
    usize activeCanvasCommandCount = 0;
    usize canvasCommandHighWater = 0;
    usize imageContentCapacity = 0;
    usize activeImageContentCount = 0;
    usize imageContentHighWater = 0;
    usize routePathCapacity = 0;
    usize routedPointerListenerCapacity = 0;
    usize activeRoutedPointerListenerCount = 0;
    usize routedPointerListenerHighWater = 0;
    usize buttonActionCapacity = 0;
    usize activeButtonActionCount = 0;
    usize buttonActionHighWater = 0;
    usize activateBehaviorCapacity = 0;
    usize activeActivateBehaviorCount = 0;
    usize activateBehaviorHighWater = 0;
    usize toggleBehaviorCapacity = 0;
    usize activeToggleBehaviorCount = 0;
    usize toggleBehaviorHighWater = 0;
    usize rangeInputBehaviorCapacity = 0;
    usize activeRangeInputBehaviorCount = 0;
    usize rangeInputBehaviorHighWater = 0;
    usize textInputBehaviorCapacity = 0;
    usize activeTextInputBehaviorCount = 0;
    usize textInputBehaviorHighWater = 0;
    usize scrollBehaviorCapacity = 0;
    usize activeScrollBehaviorCount = 0;
    usize scrollBehaviorHighWater = 0;
    usize selectBehaviorCapacity = 0;
    usize activeSelectBehaviorCount = 0;
    usize selectBehaviorHighWater = 0;
    usize textByteCapacity = 0;
    usize textByteUsed = 0;
    usize textByteHighWater = 0;
    usize liveNodeCount = 0;
    usize liveRootCount = 0;
    usize committedNodeCount = 0;
    u64 committedRevision = 0;
    usize committedLayoutNodeCount = 0;
    u64 layoutRevision = 0;
    usize committedHitNodeCount = 0;
    usize committedHitTargetCount = 0;
    u64 hitRevision = 0;
    u64 paintOrderRevision = 0;
    usize committedPaintNodeCount = 0;
    u64 paintRevision = 0;
    usize committedSemanticsNodeCount = 0;
    u64 semanticsRevision = 0;
    // Phase dirty mirrors internal UIDirty phase mask (Structure / layout /
    // HitTest / Paint / Semantics). True means that snapshot still needs a
    // successful commit before it matches live tree state.
    bool structureDirty = false;
    bool layoutDirty = false;
    bool hitDirty = false;
    bool paintDirty = false;
    bool semanticsDirty = false;
    usize lastLayoutPassCount = 0;
    usize lastLayoutMeasuredNodeCount = 0;
    usize lastLayoutArrangedNodeCount = 0;
    // Percent values skipped while an Auto axis lacked a definite Measure
    // basis. Arrange may still resolve them once against the final content box.
    usize lastLayoutPercentMeasureFallbackCount = 0;
    usize lastHitRebuildCount = 0;
    usize lastPaintCacheRebuildCount = 0;
    usize lastPaintSnapshotRebuildCount = 0;
    usize lastStyleInspectedNodeCount = 0;
    usize lastStyleResolvedNodeCount = 0;
    usize lastStyleCandidateRuleCount = 0;
    usize dirtyQueuePendingCount = 0;
    usize dirtyQueueHighWater = 0;
    UIStyleStatistics style{};
    UIComponentBuildStatistics componentBuild{};
};

class UIContext;
class UIElementBuildTransaction;

// Move-only ownership of one routed-pointer listener registration. Owner-thread
// reset takes effect immediately, including during dispatch. Off-thread reset is
// queued in bounded storage and takes effect before the next owner-thread UI
// mutation or route. Reset remains safe after the UIContext is destroyed.
class UIRoutedPointerListenerToken final {
  public:
    UIRoutedPointerListenerToken() noexcept = default;
    ~UIRoutedPointerListenerToken() noexcept;

    UIRoutedPointerListenerToken(const UIRoutedPointerListenerToken&) = delete;
    UIRoutedPointerListenerToken& operator=(const UIRoutedPointerListenerToken&) = delete;

    UIRoutedPointerListenerToken(UIRoutedPointerListenerToken&& other) noexcept;
    UIRoutedPointerListenerToken& operator=(UIRoutedPointerListenerToken&& other) noexcept;

    void reset() noexcept;
    [[nodiscard]] bool isActive() const noexcept;
    explicit operator bool() const noexcept;

  private:
    friend class UIContext;

    UIRoutedPointerListenerToken(std::weak_ptr<Detail::UIContextLifetimeControl> lifetime, u32 slot,
                                 u32 generation) noexcept;

    std::weak_ptr<Detail::UIContextLifetimeControl> m_lifetime{};
    u32 m_slot = 0;
    u32 m_generation = 0;
};

// Move-only ownership of one retained root. Owner-thread destruction reclaims
// immediately; destruction on another thread enters a bounded preallocated
// release queue drained at the next owner-thread UI mutation/commit.
class UIRootOwner final {
  public:
    UIRootOwner() noexcept = default;
    ~UIRootOwner() noexcept;

    UIRootOwner(const UIRootOwner&) = delete;
    UIRootOwner& operator=(const UIRootOwner&) = delete;

    UIRootOwner(UIRootOwner&& other) noexcept;
    UIRootOwner& operator=(UIRootOwner&& other) noexcept;

    void reset() noexcept;

    [[nodiscard]] UINodeId rootNodeId() const noexcept;
    [[nodiscard]] bool hasValue() const noexcept;
    explicit operator bool() const noexcept;

  private:
    friend class UIContext;
    friend class UIRootBuilder;
    friend class UITreeUpdater;

    UIRootOwner(std::weak_ptr<Detail::UIContextLifetimeControl> lifetime, UINodeId root) noexcept;

    std::weak_ptr<Detail::UIContextLifetimeControl> m_lifetime{};
    UINodeId m_root{};
};

// Non-owning owner-thread view. It must not outlive the UIContext that created it.
class UIRootBuilder final {
  public:
    UIRootBuilder() noexcept = default;

    [[nodiscard]] Core::Result<UIRootOwner> createRoot();
    [[nodiscard]] Core::Result<UINodeId> createElement(UINodeId parent, const UIElementDescriptor& descriptor);

  private:
    friend class UIContext;

    explicit UIRootBuilder(UIContext& context) noexcept;

    UIContext* m_context = nullptr;
};

// Move-only bounded construction of one component subtree. The root and every
// descendant remain live retained nodes, but no committed snapshot can observe
// them until the caller commits layout. Any create failure poisons the build;
// destruction or reset then reclaims the entire component subtree and its
// retained text/canvas storage.
class UIElementBuildTransaction final {
  public:
    UIElementBuildTransaction() noexcept = default;
    ~UIElementBuildTransaction() noexcept;

    UIElementBuildTransaction(const UIElementBuildTransaction&) = delete;
    UIElementBuildTransaction& operator=(const UIElementBuildTransaction&) = delete;

    UIElementBuildTransaction(UIElementBuildTransaction&& other) noexcept;
    UIElementBuildTransaction& operator=(UIElementBuildTransaction&& other) noexcept;

    [[nodiscard]] Core::Result<UINodeId> createElement(UINodeId parent,
                                                       const UIElementDescriptor& descriptor);
    [[nodiscard]] Core::Result<UINodeId> commit();
    void reset() noexcept;

    [[nodiscard]] UINodeId rootNodeId() const noexcept;
    [[nodiscard]] UIComponentBuildBudget remainingBudget() const noexcept;
    [[nodiscard]] bool isActive() const noexcept;

  private:
    friend class UIContext;
    friend class UITreeUpdater;

    UIElementBuildTransaction(UIContext& context, UINodeId updaterRoot, UINodeId componentRoot,
                              UIComponentBuildBudget remainingBudget) noexcept;

    std::weak_ptr<Detail::UIContextLifetimeControl> m_lifetime{};
    UINodeId m_updaterRoot{};
    UINodeId m_componentRoot{};
    UIComponentBuildBudget m_remainingBudget{};
    std::optional<Core::Error> m_failure{};
};

// Non-owning owner-thread view scoped to one live root. It must not outlive its
// UIContext; generation checks reject a root or child destroyed while it exists.
class UITreeUpdater final {
  public:
    UITreeUpdater() noexcept = default;

    UITreeUpdater(const UITreeUpdater&) = delete;
    UITreeUpdater& operator=(const UITreeUpdater&) = delete;

    UITreeUpdater(UITreeUpdater&& other) noexcept;
    UITreeUpdater& operator=(UITreeUpdater&& other) noexcept;

    [[nodiscard]] Core::Result<UINodeId> createElement(UINodeId parent, const UIElementDescriptor& descriptor);
    [[nodiscard]] Core::Result<UIElementBuildTransaction>
    beginBuildTransaction(UINodeId parent, const UIElementDescriptor& rootDescriptor,
                          UIComponentBuildBudget budget);
    [[nodiscard]] bool isAlive(UINodeId node) const noexcept;
    [[nodiscard]] Core::Status setLayoutStyle(UINodeId node, const UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicy(UINodeId node, UIPointerHitPolicy policy);
    // Enables or disables a published widget. Disabled widgets remain in the
    // semantics snapshot, but cannot receive default focus or widget actions.
    // The state change is owner-thread only and is committed atomically with
    // its paint/semantics invalidation.
    [[nodiscard]] Core::Status setEnabled(UINodeId node, bool enabled);
    [[nodiscard]] Core::Result<bool> isEnabled(UINodeId node) const;
    [[nodiscard]] Core::Status setFocusScopeMode(UINodeId node, UIFocusScopeMode mode);
    [[nodiscard]] Core::Result<UIFocusScopeMode> focusScopeMode(UINodeId node) const;
    [[nodiscard]] Core::Status requestFocus(UINodeId node);
    [[nodiscard]] Core::Status clearFocus();
    [[nodiscard]] Core::Status setStyleRole(UINodeId node, UIStyleRoleId role);
    [[nodiscard]] Core::Result<UIStyleRoleId> styleRole(UINodeId node) const;
    [[nodiscard]] Core::Status clearOverride(UINodeId node, UIStyleOverride properties = UIStyleOverride::All);
    [[nodiscard]] Core::Status setBoxPaint(UINodeId node, const UIBoxPaint& paint);
    [[nodiscard]] Core::Status setButtonPaint(UINodeId button, const UIButtonPaint& paint);
    [[nodiscard]] Core::Result<UIButtonPaint> buttonPaint(UINodeId button) const;
    // Intrinsic-text elements only. Stores strict UTF-8 without NUL into the
    // fixed text byte budget and dirties Measure for Auto-sized content.
    // Glyph raster and FreeType remain out of this API.
    [[nodiscard]] Core::Status setText(UINodeId node, std::string_view utf8);
    [[nodiscard]] Core::Status setTextStyle(UINodeId node, const UITextStyle& style);
    [[nodiscard]] Core::Status setContentAlignment(UINodeId node, UIContentAlignment alignment);
    [[nodiscard]] Core::Result<std::string_view> text(UINodeId node);
    [[nodiscard]] Core::Result<UITextStyle> textStyle(UINodeId node);
    [[nodiscard]] Core::Result<UIContentAlignment> contentAlignment(UINodeId node) const;
    [[nodiscard]] Core::Status setTextSelection(UINodeId textEdit, UITextSelection selection);
    [[nodiscard]] Core::Result<UITextSelection> textSelection(UINodeId textEdit) const;
    [[nodiscard]] Core::Status setTextEditPaint(UINodeId textEdit, const UITextEditPaint& paint);
    [[nodiscard]] Core::Result<UITextEditPaint> textEditPaint(UINodeId textEdit) const;
    // Activate-capable Elements own action/pressed state. Virtual collection
    // items keep their dedicated datasource action path.
    [[nodiscard]] Core::Status setButtonAction(UINodeId button, UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearButtonAction(UINodeId button);
    [[nodiscard]] Core::Result<bool> isButtonPressed(UINodeId button) const;
    // Checkbox reuses Button action slots/arm path; callback fires after toggle.
    [[nodiscard]] Core::Status setCheckboxAction(UINodeId checkbox, UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearCheckboxAction(UINodeId checkbox);
    [[nodiscard]] Core::Status setCheckboxPaint(UINodeId checkbox, const UICheckboxPaint& paint);
    [[nodiscard]] Core::Result<UICheckboxPaint> checkboxPaint(UINodeId checkbox) const;
    // Toggle-capable Elements own checked state. Checkbox-specific paint APIs
    // remain restricted to the official Checkbox recipe.
    [[nodiscard]] Core::Status setChecked(UINodeId checkbox, bool checked);
    [[nodiscard]] Core::Result<bool> isChecked(UINodeId checkbox) const;
    [[nodiscard]] Core::Result<bool> isCheckboxPressed(UINodeId checkbox) const;
    // RangeInput-capable Elements own finite min/max/value/step state. Slider
    // paint, callback, and drag geometry remain restricted to private
    // Slider-resolved nodes.
    [[nodiscard]] Core::Status setSliderRange(UINodeId slider, float minValue, float maxValue, float step = 0.0F);
    [[nodiscard]] Core::Status setSliderValue(UINodeId slider, float value);
    [[nodiscard]] Core::Result<float> sliderValue(UINodeId slider) const;
    [[nodiscard]] Core::Status setSliderPaint(UINodeId slider, const UISliderPaint& paint);
    [[nodiscard]] Core::Result<UISliderPaint> sliderPaint(UINodeId slider) const;
    [[nodiscard]] Core::Status setSliderChangeCallback(UINodeId slider, UISliderChangeCallback callback);
    [[nodiscard]] Core::Status clearSliderChangeCallback(UINodeId slider);
    [[nodiscard]] Core::Result<bool> isSliderDragging(UINodeId slider) const;
    [[nodiscard]] Core::Status setScrollViewStyle(UINodeId scrollView, const UIScrollViewStyle& style);
    [[nodiscard]] Core::Result<UIScrollViewStyle> scrollViewStyle(UINodeId scrollView) const;
    [[nodiscard]] Core::Status setScrollViewOffset(UINodeId scrollView, UIScrollOffset offset);
    [[nodiscard]] Core::Result<UIScrollOffset> scrollViewOffset(UINodeId scrollView) const;
    [[nodiscard]] Core::Result<UIScrollViewMetrics> scrollViewMetrics(UINodeId scrollView) const;
    [[nodiscard]] Core::Status setScrollViewPaint(UINodeId scrollView, const UIScrollViewPaint& paint);
    [[nodiscard]] Core::Result<UIScrollViewPaint> scrollViewPaint(UINodeId scrollView) const;
    [[nodiscard]] Core::Result<bool> isScrollViewDragging(UINodeId scrollView) const;
    [[nodiscard]] Core::Status setPopupStyle(UINodeId popup, const UIPopupStyle& style);
    [[nodiscard]] Core::Result<UIPopupStyle> popupStyle(UINodeId popup) const;
    [[nodiscard]] Core::Status setPopupOpen(UINodeId popup, bool open);
    [[nodiscard]] Core::Result<bool> isPopupOpen(UINodeId popup) const;
    [[nodiscard]] Core::Result<UIPopupMetrics> popupMetrics(UINodeId popup) const;
    [[nodiscard]] Core::Status setDropdownOpen(UINodeId dropdown, bool open);
    [[nodiscard]] Core::Result<bool> isDropdownOpen(UINodeId dropdown) const;
    [[nodiscard]] Core::Status setDropdownSelectedItem(UINodeId dropdown, UINodeId item);
    [[nodiscard]] Core::Result<UINodeId> dropdownSelectedItem(UINodeId dropdown) const;
    [[nodiscard]] Core::Result<bool> isDropdownItemSelected(UINodeId item) const;
    [[nodiscard]] Core::Status setDropdownPaint(UINodeId dropdown, const UIDropdownPaint& paint);
    [[nodiscard]] Core::Result<UIDropdownPaint> dropdownPaint(UINodeId dropdown) const;
    [[nodiscard]] Core::Status setListViewDataSource(UINodeId listView, UIListViewDataSource source);
    [[nodiscard]] Core::Status clearListViewDataSource(UINodeId listView);
    [[nodiscard]] Core::Status invalidateListViewItems(UINodeId listView);
    [[nodiscard]] Core::Status setListViewStyle(UINodeId listView, const UIListViewStyle& style);
    [[nodiscard]] Core::Result<UIListViewStyle> listViewStyle(UINodeId listView) const;
    [[nodiscard]] Core::Status setListViewPaint(UINodeId listView, const UIListViewPaint& paint);
    [[nodiscard]] Core::Result<UIListViewPaint> listViewPaint(UINodeId listView) const;
    [[nodiscard]] Core::Result<UIListViewMetrics> listViewMetrics(UINodeId listView) const;
    [[nodiscard]] Core::Status setListViewSelectedIndex(UINodeId listView, u64 logicalIndex);
    [[nodiscard]] Core::Status clearListViewSelection(UINodeId listView);
    [[nodiscard]] Core::Result<UIListViewSelection> listViewSelection(UINodeId listView) const;
    [[nodiscard]] Core::Status
    scrollListViewToIndex(UINodeId listView, u64 logicalIndex,
                          UIListViewScrollAlignment alignment = UIListViewScrollAlignment::Nearest);
    [[nodiscard]] Core::Status setTreeViewDataSource(UINodeId treeView, UITreeViewDataSource source);
    [[nodiscard]] Core::Status clearTreeViewDataSource(UINodeId treeView);
    [[nodiscard]] Core::Status invalidateTreeViewItems(UINodeId treeView);
    [[nodiscard]] Core::Status setTreeViewStyle(UINodeId treeView, const UITreeViewStyle& style);
    [[nodiscard]] Core::Result<UITreeViewStyle> treeViewStyle(UINodeId treeView) const;
    [[nodiscard]] Core::Status setTreeViewPaint(UINodeId treeView, const UITreeViewPaint& paint);
    [[nodiscard]] Core::Result<UITreeViewPaint> treeViewPaint(UINodeId treeView) const;
    [[nodiscard]] Core::Result<UITreeViewMetrics> treeViewMetrics(UINodeId treeView) const;
    [[nodiscard]] Core::Status setTreeViewSelectedIndex(UINodeId treeView, u64 logicalIndex);
    [[nodiscard]] Core::Status clearTreeViewSelection(UINodeId treeView);
    [[nodiscard]] Core::Result<UITreeViewSelection> treeViewSelection(UINodeId treeView) const;
    [[nodiscard]] Core::Status setTreeViewItemExpanded(UINodeId treeView, u64 logicalIndex, bool expanded);
    [[nodiscard]] Core::Status
    scrollTreeViewToIndex(UINodeId treeView, u64 logicalIndex,
                          UITreeViewScrollAlignment alignment = UITreeViewScrollAlignment::Nearest);
    [[nodiscard]] Core::Status setProgressBarRange(UINodeId progressBar, float minValue, float maxValue);
    [[nodiscard]] Core::Status setProgressBarValue(UINodeId progressBar, float value);
    [[nodiscard]] Core::Result<float> progressBarValue(UINodeId progressBar) const;
    [[nodiscard]] Core::Status setProgressBarPaint(UINodeId progressBar, const UIProgressBarPaint& paint);
    [[nodiscard]] Core::Result<UIProgressBarPaint> progressBarPaint(UINodeId progressBar) const;
    [[nodiscard]] Core::Status setRadioButtonPaint(UINodeId radioButton, const UIRadioButtonPaint& paint);
    [[nodiscard]] Core::Result<UIRadioButtonPaint> radioButtonPaint(UINodeId radioButton) const;
    [[nodiscard]] Core::Status setRadioButtonAction(UINodeId radioButton, UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearRadioButtonAction(UINodeId radioButton);
    [[nodiscard]] Core::Status setRadioButtonSelected(UINodeId radioButton, bool selected);
    [[nodiscard]] Core::Result<bool> isRadioButtonSelected(UINodeId radioButton) const;
    [[nodiscard]] Core::Result<bool> isRadioButtonPressed(UINodeId radioButton) const;
    [[nodiscard]] Core::Result<UIRoutedPointerListenerToken>
    addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor, UIRoutedPointerCallback callback);
    [[nodiscard]] Core::Status destroy(UINodeId node);

  private:
    friend class UIContext;

    UITreeUpdater(UIContext& context, UINodeId root) noexcept;

    UIContext* m_context = nullptr;
    UINodeId m_root{};
};

// Single-owner-thread retained-tree context for one WindowId. The supplied PMR
// resource backs bounded tree/id/style/paint/dirty/layout/hit scratch and
// committed snapshot storage and must outlive the context. Small control-plane
// objects and cross-thread release queues allocate only during Create() from the process
// default heap.
class UIContext final {
  public:
    // Default Create wires createPlaceholderTextRasterizer, opens its built-in
    // empty face, and owns a fixed-capacity UIGlyphAtlas for paint-time
    // placements. Glyph DisplayList emission depends on successful atlas insert.
    [[nodiscard]] static Core::Result<std::unique_ptr<UIContext>>
    Create(Platform::WindowId ownerWindow, UIContextCapacityConfig capacityConfig = {},
           std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    // Takes ownership of textRasterizer. For the placeholder rasterizer, empty
    // font bytes open the built-in face. FreeType adapters must open a real face
    // before setText can measure (or measure fails with InvalidFont).
    [[nodiscard]] static Core::Result<std::unique_ptr<UIContext>>
    Create(Platform::WindowId ownerWindow, UIContextCapacityConfig capacityConfig,
           std::unique_ptr<IUITextRasterizer> textRasterizer,
           std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    // Destruction is an owner-thread, phase-boundary operation. Destroying a
    // context from one of its routed callbacks/callback destructors, or from a
    // non-owner thread, violates the lifetime contract and terminates rather
    // than continuing with a use-after-free.
    ~UIContext() noexcept;

    UIContext(const UIContext&) = delete;
    UIContext& operator=(const UIContext&) = delete;
    UIContext(UIContext&&) = delete;
    UIContext& operator=(UIContext&&) = delete;

    [[nodiscard]] Platform::WindowId ownerWindow() const noexcept;
    [[nodiscard]] bool contains(UINodeId node) const noexcept;

    // Active product theme for default control chrome. Defaults to
    // makeDefaultProductTheme(). setProductTheme validates and atomically
    // re-themes existing properties that still inherit product chrome. A local
    // setBoxPaint / set*Paint / setTextStyle call detaches only that property;
    // new nodes inherit the latest theme. Owner-thread only.
    [[nodiscard]] const UITheme& productTheme() const noexcept;
    [[nodiscard]] Core::Status setProductTheme(const UITheme& theme);
    // Startup-only stylesheet registration. Both operations require the owner
    // thread and no live retained nodes. installStyleSheet copies all rules and
    // atomically replaces the previous compiled sheet on success.
    [[nodiscard]] Core::Result<UIStyleClassId> registerStyleClass();
    [[nodiscard]] Core::Status installStyleSheet(
        std::span<const UIStyleBoxFillRule> rules);

    // Opens (or replaces) the text face used by measure/paint. Closes the previous
    // face, clears the glyph atlas, and dirties layout/paint for nodes with text.
    // FreeType requires non-empty font bytes; placeholder rejects non-empty bytes.
    [[nodiscard]] Core::Status openTextFont(std::span<const std::byte> fontBytes, i32 faceIndex = 0);

    [[nodiscard]] UIRootBuilder rootBuilder() noexcept;
    [[nodiscard]] Core::Result<UITreeUpdater> treeUpdater(UIRootOwner& rootOwner);

    // C1a diagnostic seam. Runtime frame publication uses commitLayout() so a
    // pending structure and its geometry become visible in one transaction.
    [[nodiscard]] Core::Status commitStructure();
    [[nodiscard]] UICommittedStructureView committedStructure() const noexcept;
    [[nodiscard]] Core::Status commitLayout(UILogicalSize viewportSize);
    [[nodiscard]] UICommittedLayoutView committedLayout() const noexcept;
    [[nodiscard]] UICommittedHitView committedHit() const noexcept;
    [[nodiscard]] UICommittedPaintView committedPaint() const noexcept;
    // M11-C4: owner-thread accessible semantics snapshot (interactive kinds).
    [[nodiscard]] UICommittedSemanticsView committedSemantics() const noexcept;
    // Borrow of the context-owned R8 glyph atlas page after paint publication.
    // Valid until the next paint that inserts/clears the atlas, or Context
    // destruction. Empty when no atlas is allocated.
    [[nodiscard]] std::span<const u8> glyphAtlasPixels() const noexcept;
    [[nodiscard]] u32 glyphAtlasWidth() const noexcept;
    [[nodiscard]] u32 glyphAtlasHeight() const noexcept;
    // Pure query over the last committed hit snapshot. It never commits
    // layout, rebuilds hit data, dispatches an event, or allocates storage.
    [[nodiscard]] UIPointerHitQueryResult queryPointerHit(UILogicalPoint point) const noexcept;
    // Registers an owning, fixed-inline callback in stable per-node order.
    // Registration is bounded; dispatch never grows or heap-falls back.
    [[nodiscard]] Core::Result<UIRoutedPointerListenerToken>
    addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor, UIRoutedPointerCallback callback);
    // Routes one normalized pointer input over the last committed hit snapshot.
    // It performs at most one point query and never commits layout or hit data.
    [[nodiscard]] Core::Result<UIPointerRouteResult> routePointerInput(const UIPointerInputEvent& input);
    // Clears retained Primary Pointer interaction state for the matching
    // Window without synthesizing an Up event or invoking a Button action.
    [[nodiscard]] Core::Status cancelPointerInteraction(Platform::WindowId routedWindow);
    // Clears a disconnected gamepad's default-action press without releasing
    // another keyboard/gamepad control. A missing gamepad clears all default
    // action controls for the window.
    [[nodiscard]] Core::Status
    cancelDefaultActionInteraction(Platform::WindowId routedWindow,
                                   std::optional<Platform::GamepadId> gamepad = std::nullopt);
    // Activates the default-focused Button (set when Primary Pointer arms a
    // Button). Used for keyboard Enter/Space and Gamepad South Accept.
    // Returns consumed=true when an action was invoked or the key was claimed.
    struct UIDefaultActionResult final {
        bool consumed = false;
        bool activated = false;
    };
    [[nodiscard]] Core::Result<UIDefaultActionResult>
    routeDefaultActionActivate(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                               UIButtonActivationSource source,
                               std::optional<Platform::DigitalControlIdentity> control = std::nullopt);
    // Releases one keyboard/gamepad default-action control. The control
    // identity is required so overlapping Accept keys or gamepads cannot
    // release one another's pressed state.
    [[nodiscard]] Core::Result<UIDefaultActionResult>
    routeDefaultActionRelease(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                              UIButtonActivationSource source, const Platform::DigitalControlIdentity& control);
    // Cycles keyboard focus among visible Targetable Button/Checkbox/RadioButton/TextEdit nodes in paint
    // order. reverse=true moves backward (Shift+Tab). A committed Contain
    // Focus Scope, including Modal, keeps traversal inside its subtree.
    struct UIDefaultFocusStepResult final {
        bool consumed = false;
        bool moved = false;
        UINodeId focus{};
    };
    [[nodiscard]] Core::Result<UIDefaultFocusStepResult> routeDefaultActionFocusStep(bool reverse);
    // Moves an existing focus geometrically among enabled committed candidates.
    // Navigation never wraps. A successful move claims its matching release;
    // no focus or no candidate leaves gameplay input unconsumed.
    [[nodiscard]] Core::Result<UIDefaultFocusStepResult>
    routeFocusNavigation(UIFocusNavigationDirection direction, bool pressed = true);
    // Adjusts the focused RangeInput without reusing spatial focus commands.
    // A successful Down latches its exact physical control so the matching Up
    // remains consumed after focus or retained-state changes. step=0 uses one
    // percent of the finite range as the command increment.
    [[nodiscard]] Core::Result<UIRangeInputCommandResult>
    routeRangeInputCommand(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                           UIRangeInputCommand command, bool pressed,
                           const Platform::DigitalControlIdentity& control);
    // Routes retained Dropdown navigation. pressed=false releases a command
    // previously claimed on key-down without repeating its state transition.
    [[nodiscard]] Core::Result<UIDropdownCommandResult> routeDropdownCommand(UIDropdownCommand command, bool pressed);
    [[nodiscard]] Core::Result<UIListViewCommandResult> routeListViewCommand(UIListViewCommand command, bool pressed);
    [[nodiscard]] Core::Result<UITreeViewCommandResult> routeTreeViewCommand(UITreeViewCommand command, bool pressed);
    [[nodiscard]] UINodeId defaultActionFocus() const noexcept;
    [[nodiscard]] UINodeId activeFocusScope() const noexcept;
    [[nodiscard]] UINodeId activeModal() const noexcept;
    [[nodiscard]] UINodeId pointerCapture() const noexcept;
    [[nodiscard]] UINodeId activePopup() const noexcept;
    // Explicit focus changes use the last committed hit/focus-scope snapshot.
    // A target must be visible, enabled, Targetable, keyboard-focusable, and
    // inside the active Modal. clearFocus is idempotent.
    [[nodiscard]] Core::Status requestFocus(UINodeId node);
    [[nodiscard]] Core::Status clearFocus();
    // Synchronous owner-thread action seam shared by UIA/AT-SPI adapters. It
    // preserves normal control callbacks and rejects stale, disabled, or
    // kind-incompatible targets without partially changing retained state.
    [[nodiscard]] Core::Status performAccessibilityAction(const UIAccessibilityAction& action);

    // IME/text target. Pointer-down or Tab focus on a TextEdit sets text focus.
    // Composition preedit is retained separately; commit replaces the selection.
    struct UITextInputRouteResult final {
        bool consumed = false;
        bool applied = false;
    };
    [[nodiscard]] UINodeId imeFocus() const noexcept;
    [[nodiscard]] bool imeCompositionActive() const noexcept;
    [[nodiscard]] std::string_view imePreeditUtf8() const noexcept;
    [[nodiscard]] u32 imePreeditCursorCodepoint() const noexcept;
    [[nodiscard]] Core::Result<UITextInputRouteResult>
    routeTextComposition(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                         std::string_view preeditUtf8, u32 cursorCodepoint, Platform::TextCompositionStage stage);
    [[nodiscard]] Core::Result<UITextInputRouteResult> routeTextInput(Platform::WindowId window,
                                                                      Platform::PlatformFrameId platformFrame,
                                                                      u64 sourceSequence,
                                                                      std::string_view committedUtf8);
    [[nodiscard]] Core::Result<UITextInputRouteResult>
    routeTextEditCommand(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                         UITextEditCommand command, bool extendSelection = false);

    [[nodiscard]] UIContextStatistics statistics() const noexcept;
    [[nodiscard]] usize liveNodeCount() const noexcept;
    [[nodiscard]] usize liveRootCount() const noexcept;

  private:
    friend class UIRootOwner;
    friend class UIRootBuilder;
    friend class UITreeUpdater;
    friend class UIElementBuildTransaction;
    friend class UIRoutedPointerListenerToken;

    struct Impl;

    explicit UIContext(std::unique_ptr<Impl> impl) noexcept;

    [[nodiscard]] Core::Result<UIRootOwner> createRoot();
    [[nodiscard]] Core::Result<UINodeId> createElement(UINodeId parent, const UIElementDescriptor& descriptor);
    [[nodiscard]] Core::Result<UINodeId> createElementFromUpdater(UINodeId updaterRoot, UINodeId parent,
                                                                  const UIElementDescriptor& descriptor);
    [[nodiscard]] Core::Result<UIElementBuildTransaction>
    beginBuildTransactionFromUpdater(UINodeId updaterRoot, UINodeId parent,
                                     const UIElementDescriptor& rootDescriptor,
                                     UIComponentBuildBudget budget);
    [[nodiscard]] Core::Result<UINodeId>
    createElementFromBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot, UINodeId parent,
                                      const UIElementDescriptor& descriptor,
                                      UIComponentBuildBudget& remainingBudget);
    [[nodiscard]] Core::Status commitBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot,
                                                      UIComponentBuildBudget& remainingBudget);
    void rollbackBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot,
                                  UIComponentBuildBudget& remainingBudget) noexcept;
    [[nodiscard]] bool isBuildTransactionActive(UINodeId componentRoot) const noexcept;
    [[nodiscard]] Core::Status setProgressBarRangeFromUpdater(UINodeId updaterRoot, UINodeId progressBar,
                                                              float minValue, float maxValue);
    [[nodiscard]] Core::Status setProgressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar, float value);
    [[nodiscard]] Core::Result<float> progressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar) const;
    [[nodiscard]] Core::Status setProgressBarPaintFromUpdater(UINodeId updaterRoot, UINodeId progressBar,
                                                              const UIProgressBarPaint& paint);
    [[nodiscard]] Core::Result<UIProgressBarPaint> progressBarPaintFromUpdater(UINodeId updaterRoot,
                                                                               UINodeId progressBar) const;
    [[nodiscard]] Core::Status setRadioButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                              const UIRadioButtonPaint& paint);
    [[nodiscard]] Core::Result<UIRadioButtonPaint> radioButtonPaintFromUpdater(UINodeId updaterRoot,
                                                                               UINodeId radioButton) const;
    [[nodiscard]] Core::Status setRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                               UIButtonActionCallback&& callback);
    [[nodiscard]] Core::Status clearRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton);
    [[nodiscard]] Core::Status setRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                                 bool selected);
    [[nodiscard]] Core::Result<bool> isRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton) const;
    [[nodiscard]] Core::Result<bool> isRadioButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId radioButton) const;
    [[nodiscard]] Core::Status setLayoutStyleFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                         const UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicyFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                              UIPointerHitPolicy policy);
    [[nodiscard]] Core::Status setEnabledFromUpdater(UINodeId updaterRoot, UINodeId node, bool enabled);
    [[nodiscard]] Core::Status setFocusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node, UIFocusScopeMode mode);
    [[nodiscard]] Core::Result<UIFocusScopeMode> focusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node) const;
    [[nodiscard]] Core::Status requestFocusFromUpdater(UINodeId updaterRoot, UINodeId node);
    [[nodiscard]] Core::Status clearFocusFromUpdater(UINodeId updaterRoot);
    [[nodiscard]] Core::Status setStyleRoleFromUpdater(UINodeId updaterRoot, UINodeId node, UIStyleRoleId role);
    [[nodiscard]] Core::Result<UIStyleRoleId> styleRoleFromUpdater(UINodeId updaterRoot, UINodeId node) const;
    [[nodiscard]] Core::Status clearOverrideFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                       UIStyleOverride properties);
    [[nodiscard]] Core::Result<bool> isEnabledFromUpdater(UINodeId updaterRoot, UINodeId node) const;
    [[nodiscard]] Core::Status setBoxPaintFromUpdater(UINodeId updaterRoot, UINodeId node, const UIBoxPaint& paint);
    [[nodiscard]] Core::Status setButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId button,
                                                         const UIButtonPaint& paint);
    [[nodiscard]] Core::Result<UIButtonPaint> buttonPaintFromUpdater(UINodeId updaterRoot, UINodeId button) const;
    [[nodiscard]] Core::Status setTextFromUpdater(UINodeId updaterRoot, UINodeId node, std::string_view utf8);
    [[nodiscard]] Core::Status setTextStyleFromUpdater(UINodeId updaterRoot, UINodeId node, const UITextStyle& style);
    [[nodiscard]] Core::Status setContentAlignmentFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                              UIContentAlignment alignment);
    [[nodiscard]] Core::Result<std::string_view> textFromUpdater(UINodeId updaterRoot, UINodeId node);
    [[nodiscard]] Core::Result<UITextStyle> textStyleFromUpdater(UINodeId updaterRoot, UINodeId node);
    [[nodiscard]] Core::Result<UIContentAlignment> contentAlignmentFromUpdater(UINodeId updaterRoot,
                                                                               UINodeId node) const;
    [[nodiscard]] Core::Status setTextSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit,
                                                           UITextSelection selection);
    [[nodiscard]] Core::Result<UITextSelection> textSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit) const;
    [[nodiscard]] Core::Status setTextEditPaintFromUpdater(UINodeId updaterRoot, UINodeId textEdit,
                                                           const UITextEditPaint& paint);
    [[nodiscard]] Core::Result<UITextEditPaint> textEditPaintFromUpdater(UINodeId updaterRoot,
                                                                         UINodeId textEdit) const;
    [[nodiscard]] Core::Status setButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button,
                                                          UIButtonActionCallback&& callback);
    [[nodiscard]] Core::Status clearButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button);
    [[nodiscard]] Core::Result<bool> isButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId button);
    [[nodiscard]] Core::Status setCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                            UIButtonActionCallback&& callback);
    [[nodiscard]] Core::Status clearCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox);
    [[nodiscard]] Core::Status setCheckboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                           const UICheckboxPaint& paint);
    [[nodiscard]] Core::Result<UICheckboxPaint> checkboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const;
    [[nodiscard]] Core::Status setCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox, bool checked);
    [[nodiscard]] Core::Result<bool> isCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const;
    [[nodiscard]] Core::Result<bool> isCheckboxPressedFromUpdater(UINodeId updaterRoot, UINodeId checkbox);
    [[nodiscard]] Core::Status setSliderRangeFromUpdater(UINodeId updaterRoot, UINodeId slider, float minValue,
                                                         float maxValue, float step);
    [[nodiscard]] Core::Status setSliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider, float value);
    [[nodiscard]] Core::Result<float> sliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider) const;
    [[nodiscard]] Core::Status setSliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider,
                                                         const UISliderPaint& paint);
    [[nodiscard]] Core::Result<UISliderPaint> sliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider) const;
    [[nodiscard]] Core::Status setSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider,
                                                                  UISliderChangeCallback&& callback);
    [[nodiscard]] Core::Status clearSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider);
    [[nodiscard]] Core::Result<bool> isSliderDraggingFromUpdater(UINodeId updaterRoot, UINodeId slider) const;
    [[nodiscard]] Core::Status setScrollViewStyleFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                            const UIScrollViewStyle& style);
    [[nodiscard]] Core::Result<UIScrollViewStyle> scrollViewStyleFromUpdater(UINodeId updaterRoot,
                                                                             UINodeId scrollView) const;
    [[nodiscard]] Core::Status setScrollViewOffsetFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                             UIScrollOffset offset);
    [[nodiscard]] Core::Result<UIScrollOffset> scrollViewOffsetFromUpdater(UINodeId updaterRoot,
                                                                           UINodeId scrollView) const;
    [[nodiscard]] Core::Result<UIScrollViewMetrics> scrollViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                                 UINodeId scrollView) const;
    [[nodiscard]] Core::Status setScrollViewPaintFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                            const UIScrollViewPaint& paint);
    [[nodiscard]] Core::Result<UIScrollViewPaint> scrollViewPaintFromUpdater(UINodeId updaterRoot,
                                                                             UINodeId scrollView) const;
    [[nodiscard]] Core::Result<bool> isScrollViewDraggingFromUpdater(UINodeId updaterRoot,
                                                                     UINodeId scrollView) const;
    [[nodiscard]] Core::Status setPopupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup,
                                                       const UIPopupStyle& style);
    [[nodiscard]] Core::Result<UIPopupStyle> popupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup) const;
    [[nodiscard]] Core::Status setPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup, bool open);
    [[nodiscard]] Core::Result<bool> isPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup) const;
    [[nodiscard]] Core::Result<UIPopupMetrics> popupMetricsFromUpdater(UINodeId updaterRoot, UINodeId popup) const;
    [[nodiscard]] Core::Status setDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown, bool open);
    [[nodiscard]] Core::Result<bool> isDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown) const;
    [[nodiscard]] Core::Status setDropdownSelectedItemFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
                                                                 UINodeId item);
    [[nodiscard]] Core::Result<UINodeId> dropdownSelectedItemFromUpdater(UINodeId updaterRoot,
                                                                         UINodeId dropdown) const;
    [[nodiscard]] Core::Result<bool> isDropdownItemSelectedFromUpdater(UINodeId updaterRoot, UINodeId item) const;
    [[nodiscard]] Core::Status setDropdownPaintFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
                                                          const UIDropdownPaint& paint);
    [[nodiscard]] Core::Result<UIDropdownPaint> dropdownPaintFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId dropdown) const;
    [[nodiscard]] Core::Status setListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                               UIListViewDataSource source);
    [[nodiscard]] Core::Status clearListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView);
    [[nodiscard]] Core::Status invalidateListViewItemsFromUpdater(UINodeId updaterRoot, UINodeId listView);
    [[nodiscard]] Core::Status setListViewStyleFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                          const UIListViewStyle& style);
    [[nodiscard]] Core::Result<UIListViewStyle> listViewStyleFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId listView) const;
    [[nodiscard]] Core::Status setListViewPaintFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                          const UIListViewPaint& paint);
    [[nodiscard]] Core::Result<UIListViewPaint> listViewPaintFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId listView) const;
    [[nodiscard]] Core::Result<UIListViewMetrics> listViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                            UINodeId listView) const;
    [[nodiscard]] Core::Status setListViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                                  u64 logicalIndex);
    [[nodiscard]] Core::Status clearListViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId listView);
    [[nodiscard]] Core::Result<UIListViewSelection> listViewSelectionFromUpdater(UINodeId updaterRoot,
                                                                                UINodeId listView) const;
    [[nodiscard]] Core::Status scrollListViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                                u64 logicalIndex, UIListViewScrollAlignment alignment);
    [[nodiscard]] Core::Status setTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                                UITreeViewDataSource source);
    [[nodiscard]] Core::Status clearTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView);
    [[nodiscard]] Core::Status invalidateTreeViewItemsFromUpdater(UINodeId updaterRoot, UINodeId treeView);
    [[nodiscard]] Core::Status setTreeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                           const UITreeViewStyle& style);
    [[nodiscard]] Core::Result<UITreeViewStyle> treeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView) const;
    [[nodiscard]] Core::Status setTreeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                           const UITreeViewPaint& paint);
    [[nodiscard]] Core::Result<UITreeViewPaint> treeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView) const;
    [[nodiscard]] Core::Result<UITreeViewMetrics> treeViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                             UINodeId treeView) const;
    [[nodiscard]] Core::Status setTreeViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                                   u64 logicalIndex);
    [[nodiscard]] Core::Status clearTreeViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId treeView);
    [[nodiscard]] Core::Result<UITreeViewSelection> treeViewSelectionFromUpdater(UINodeId updaterRoot,
                                                                                 UINodeId treeView) const;
    [[nodiscard]] Core::Status setTreeViewItemExpandedFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                                  u64 logicalIndex, bool expanded);
    [[nodiscard]] Core::Status scrollTreeViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                                u64 logicalIndex, UITreeViewScrollAlignment alignment);
    [[nodiscard]] Core::Result<UIRoutedPointerListenerToken>
    addRoutedPointerListenerFromUpdater(UINodeId updaterRoot, UIRoutedPointerListenerDesc descriptor,
                                        UIRoutedPointerCallback&& callback);
    [[nodiscard]] Core::Status destroyNodeFromUpdater(UINodeId updaterRoot, UINodeId node);
    void destroyRootFromOwner(UINodeId root) noexcept;
    [[nodiscard]] bool isAliveInRoot(UINodeId updaterRoot, UINodeId node) const noexcept;
    void releaseRoutedPointerListenerFromToken(u32 slot, u32 generation) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace Tina::UI
