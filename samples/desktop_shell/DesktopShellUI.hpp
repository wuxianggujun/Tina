#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/runtime/PhaseContexts.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <optional>

namespace Tina::SampleUI {

using Core::u32;
using Core::u64;

enum class ShellTheme : Core::u8 {
    Dark = 0,
    Light,
};

// Which workspace panes the responsive rules keep visible. The shell never
// scales typography to fit; it collapses whole panes instead.
enum class ShellResponsiveTier : Core::u8 {
    // >= 1280 logical px: left dock, viewport, inspector and timeline.
    Full = 0,
    // 960..1279: docks clamp to their minimums, timeline collapses.
    Compressed,
    // < 960: inspector is opt-in through an explicit command.
    Minimal,
};

enum class ShellDocument : Core::u8 {
    Scene = 0,
    Material,
    Script,
};

// Application-owned durable model. The retained UI tree is rebuilt from this on
// a density change, so nothing here may live only inside UI nodes.
struct DesktopShellState final {
    ShellTheme theme = ShellTheme::Dark;
    UI::UIDensity density = UI::UIDensity::Compact;

    // Pane intent, not committed geometry. UI publishes the resolved rects.
    float leftDockFraction = 0.0F;
    float inspectorFraction = 0.0F;
    float timelineFraction = 0.0F;
    bool timelineCollapsed = false;
    bool inspectorVisible = true;

    ShellDocument activeDocument = ShellDocument::Scene;
    UI::UITreeViewItemKey hierarchySelection{};
    UI::UIListViewItemKey assetSelection{};
    bool worldExpanded = true;
    bool lightingExpanded = false;

    bool dialogOpen = false;
    bool menuOpen = false;

    // Registered once per window and reused by every replacement root.
    UI::UIStyleClassId accentClass{};
    UI::UIStyleTokenId accentToken{};
    bool styleRegistered = false;
};

// Structured evidence for the sample gate. Counters accumulate across roots.
struct DesktopShellSnapshot final {
    ShellTheme theme = ShellTheme::Dark;
    UI::UIDensity density = UI::UIDensity::Compact;
    ShellResponsiveTier tier = ShellResponsiveTier::Full;
    ShellDocument activeDocument = ShellDocument::Scene;

    u32 splitViewCount = 0;
    u32 commandCount = 0;
    u32 bandCount = 0;
    u32 inspectorRowCount = 0;

    float leftDockWidth = 0.0F;
    float viewportWidth = 0.0F;
    float viewportHeight = 0.0F;
    float inspectorWidth = 0.0F;
    float timelineHeight = 0.0F;
    float statusBarHeight = 0.0F;

    u64 themeSwitches = 0;
    u64 densitySwitchRequests = 0;
    u64 documentSwitches = 0;
    u64 splitterDrags = 0;
    u64 commandActivations = 0;

    bool viewportUnobstructed = false;
    bool timelineCollapsed = false;
    bool inspectorVisible = true;
    bool dialogOpen = false;
    bool menuOpen = false;
    bool rootAlive = false;
};

// One retained root composed from three nested SplitViews. It owns no second UI
// runtime: every pane is an ordinary Element subtree in the primary window root.
class DesktopShellUI final {
  public:
    DesktopShellUI() noexcept = default;

    DesktopShellUI(const DesktopShellUI&) = delete;
    DesktopShellUI& operator=(const DesktopShellUI&) = delete;

    [[nodiscard]] Core::Status build(GameStateEnterContext& context, DesktopShellState& state);
    [[nodiscard]] Core::Status update(UIUpdateContext& context);
    void release() noexcept;

    // Logical width of the last committed publication, used by the responsive
    // rules. Supplied by the host from primary-window metrics.
    void setLogicalWidth(float logicalWidth) noexcept;

    [[nodiscard]] DesktopShellSnapshot snapshot() const noexcept;

  private:
    struct Nodes final {
        UI::UINodeId background{};
        UI::UINodeId commandBar{};
        UI::UINodeId accentRule{};
        UI::UINodeId brandLabel{};
        UI::UINodeId documentTabs{};
        UI::UINodeId contextToolbar{};
        UI::UINodeId cameraLabel{};
        UI::UINodeId workspace{};
        UI::UINodeId statusBar{};
        UI::UINodeId statusLabel{};

        // Workspace SplitView A: left dock | main.
        UI::UINodeId splitA{};
        UI::UINodeId leftDock{};
        UI::UINodeId splitterA{};
        UI::UINodeId main{};

        // SplitView B: center | inspector.
        UI::UINodeId splitB{};
        UI::UINodeId center{};
        UI::UINodeId splitterB{};
        UI::UINodeId inspector{};

        // SplitView C: viewport | timeline.
        UI::UINodeId splitC{};
        UI::UINodeId viewport{};
        UI::UINodeId splitterC{};
        UI::UINodeId timeline{};

        UI::UINodeId hierarchyTree{};
        UI::UINodeId assetList{};
        UI::UINodeId viewportSurface{};
        UI::UINodeId viewportHint{};
        UI::UINodeId timelineLabel{};
        UI::UINodeId inspectorTitle{};

        UI::UINodeId menuAnchor{};
        UI::UINodeId menu{};
        UI::UINodeId dialogModal{};

        // Document tab strip reuses RadioButton selection; three documents.
        std::array<UI::UINodeId, 3> documentTabButtons{};
    };

    // Band builders. Each appends one direct child of nodes_.background so the
    // shell keeps a single Column of bands.
    [[nodiscard]] Core::Status buildCommandBar(PrimaryWindowUITreeUpdater& tree, const UI::UITheme& theme);
    [[nodiscard]] Core::Status buildDocumentTabs(PrimaryWindowUITreeUpdater& tree, const UI::UITheme& theme);
    [[nodiscard]] Core::Status buildContextToolbar(PrimaryWindowUITreeUpdater& tree, const UI::UITheme& theme);
    [[nodiscard]] Core::Status buildWorkspace(PrimaryWindowUITreeUpdater& tree, const UI::UITheme& theme);
    [[nodiscard]] Core::Status buildStatusBar(PrimaryWindowUITreeUpdater& tree, const UI::UITheme& theme);

    // Pane content. Each fills one committed SplitView pane.
    [[nodiscard]] Core::Status buildLeftDockContent(PrimaryWindowUITreeUpdater& tree,
                                                    const UI::UITheme& theme);
    [[nodiscard]] Core::Status buildViewportContent(PrimaryWindowUITreeUpdater& tree,
                                                    const UI::UITheme& theme);
    [[nodiscard]] Core::Status buildInspectorContent(PrimaryWindowUITreeUpdater& tree,
                                                     const UI::UITheme& theme);
    [[nodiscard]] Core::Status buildTimelineContent(PrimaryWindowUITreeUpdater& tree,
                                                    const UI::UITheme& theme);

    // Reads committed SplitView metrics into the snapshot and checks that the
    // viewport keeps its minimum unobstructed area.
    [[nodiscard]] Core::Status refreshCommittedGeometry(PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Core::Status applyResponsiveTier(PrimaryWindowUITreeUpdater& tree);

    // Borrowed collection projections. The shell owns the backing arrays, so
    // labels outlive every binding.
    [[nodiscard]] UI::UIListViewDataSource listDataSource() const noexcept;
    [[nodiscard]] UI::UITreeViewDataSource treeDataSource() noexcept;
    static u64 assetItemCount(const void* state) noexcept;
    static bool resolveAssetItem(const void* state, u64 logicalIndex,
                                 UI::UIListViewItemDescriptor& output) noexcept;
    static u64 hierarchyItemCount(const void* state) noexcept;
    static bool resolveHierarchyItem(const void* state, u64 logicalIndex,
                                     UI::UITreeViewItemDescriptor& output) noexcept;
    static bool setHierarchyItemExpanded(void* state, u64 logicalIndex, bool expanded) noexcept;

    DesktopShellState* state_ = nullptr;
    std::optional<UI::UIRootOwner> root_{};
    Nodes nodes_{};

    ShellTheme currentTheme_ = ShellTheme::Dark;
    UI::UIDensity density_ = UI::UIDensity::Compact;
    UI::UIDensity initialDensity_ = UI::UIDensity::Compact;
    ShellResponsiveTier tier_ = ShellResponsiveTier::Full;

    u32 splitViewCount_ = 0;
    u32 commandCount_ = 0;
    u32 bandCount_ = 0;
    u32 inspectorRowCount_ = 0;
    u64 themeSwitches_ = 0;
    u64 densitySwitchRequests_ = 0;
    u64 documentSwitches_ = 0;
    u64 splitterDrags_ = 0;
    u64 commandActivations_ = 0;

    // Committed geometry mirrored from the last successful publication.
    float logicalWidth_ = 0.0F;
    float leftDockWidth_ = 0.0F;
    float viewportWidth_ = 0.0F;
    float viewportHeight_ = 0.0F;
    float inspectorWidth_ = 0.0F;
    float timelineHeight_ = 0.0F;
    float statusBarHeight_ = 0.0F;
    bool viewportUnobstructed_ = false;

    bool statusDirty_ = false;
    bool layoutDirty_ = false;
};

} // namespace Tina::SampleUI
