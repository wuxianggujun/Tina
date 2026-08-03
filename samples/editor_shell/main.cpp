// Read-only editor shell sample: proves Tina::UI can host tool chrome
// (Hierarchy TreeView + Inspector + Viewport placeholder). No authoring
// writes, no Tina::Editor module, not 2D-EDITOR product tooling.

#include <tina/core/error/Error.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIStyle.hpp>
#include <tina/ui/UITheme.hpp>
#include <tina/ui/UITreeView.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace {

namespace UI = Tina::UI;

using Tina::Core::u32;
using Tina::Core::u64;

inline constexpr u64 DefaultFrameCount = 60;
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;
inline constexpr u32 WindowLogicalWidth = 1280;
inline constexpr u32 WindowLogicalHeight = 800;
inline constexpr u32 HierarchyMaterializedCapacity = 16;

inline constexpr UI::UITreeViewItemKey SceneRootKey = 1;
inline constexpr UI::UITreeViewItemKey CameraKey = 2;
inline constexpr UI::UITreeViewItemKey PlayerKey = 3;
inline constexpr UI::UITreeViewItemKey PlayerSpriteKey = 4;
inline constexpr UI::UITreeViewItemKey PlayerTransformKey = 5;
inline constexpr UI::UITreeViewItemKey LightsKey = 6;
inline constexpr UI::UITreeViewItemKey TileMapKey = 7;

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
    bool autoSelectDemo = true;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u64 uiRootsCreated = 0;
    u64 uiRootsReleased = 0;
    u64 hierarchySelectionChanges = 0;
    UI::UITreeViewItemKey finalSelectionKey = UI::InvalidUITreeViewItemKey;
    u64 finalSelectionIndex = 0;
    u64 hierarchyLogicalItems = 0;
    u64 styleRegisteredClasses = 0;
    u64 styleRegisteredTokens = 0;
    u64 styleActiveRules = 0;
    u64 styleRevision = 0;
    u64 styleTokenUpdates = 0;
    bool selectionVerified = false;
    bool stylesheetInstalled = false;
    bool readOnlyShell = true;
};

void writeJsonString(std::ostream& output, std::string_view value)
{
    constexpr char Hexadecimal[] = "0123456789abcdef";
    output.put('"');
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (byte < 0x20U) {
                output << "\\u00" << Hexadecimal[byte >> 4U] << Hexadecimal[byte & 0x0FU];
            } else {
                output.put(static_cast<char>(byte));
            }
            break;
        }
    }
    output.put('"');
}

void writeError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_editor_shell\",\"domain\":"
              << static_cast<std::uint16_t>(error.code.domain) << ",\"code\":" << error.code.value
              << ",\"message\":";
    writeJsonString(std::cerr, error.message);
    std::cerr << "}\n";
}

template <typename Value>
[[nodiscard]] bool parseUnsigned(std::string_view text, Value& value) noexcept
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argumentCount, char** arguments)
{
    constexpr std::string_view FramesPrefix = "--frames=";
    constexpr std::string_view DelayPrefix = "--frame-delay-ms=";

    SampleOptions options{};
    bool hasFrames = false;
    bool hasDelay = false;
    for (int index = 1; index < argumentCount; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument.starts_with(FramesPrefix)) {
            if (hasFrames) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --frames argument");
            }
            const std::string_view value = argument.substr(FramesPrefix.size());
            if (!parseUnsigned(value, options.targetFrameCount) || options.targetFrameCount == 0) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frames must be an unsigned integer greater than zero");
            }
            hasFrames = true;
            continue;
        }
        if (argument.starts_with(DelayPrefix)) {
            if (hasDelay) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --frame-delay-ms argument");
            }
            const std::string_view value = argument.substr(DelayPrefix.size());
            if (!parseUnsigned(value, options.frameDelayMilliseconds)) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frame-delay-ms must be an unsigned integer");
            }
            hasDelay = true;
            continue;
        }
        if (argument == "--no-auto-select") {
            options.autoSelectDemo = false;
            continue;
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "Unsupported command-line argument");
    }
    return options;
}

[[nodiscard]] UI::UILayoutStyle percentSize(float widthPercent, float heightPercent) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Percent(widthPercent);
    style.size.height = UI::UILayoutLength::Percent(heightPercent);
    return style;
}

[[nodiscard]] UI::UILayoutStyle flexChild(float grow, float basisPercent, float heightPercent) noexcept
{
    UI::UILayoutStyle style = percentSize(basisPercent, heightPercent);
    style.flexItem.grow = grow;
    style.flexItem.shrink = 1.0F;
    style.flexItem.basis = UI::UILayoutLength::Percent(basisPercent);
    return style;
}

[[nodiscard]] std::string_view hierarchyLabel(UI::UITreeViewItemKey key) noexcept
{
    switch (key) {
    case SceneRootKey:
        return "Scene";
    case CameraKey:
        return "Camera2D";
    case PlayerKey:
        return "Player";
    case PlayerSpriteKey:
        return "SpriteRenderer2D";
    case PlayerTransformKey:
        return "Transform";
    case LightsKey:
        return "PointLight2D";
    case TileMapKey:
        return "TileMap";
    default:
        return "Unknown";
    }
}

[[nodiscard]] std::string_view hierarchyKind(UI::UITreeViewItemKey key) noexcept
{
    switch (key) {
    case SceneRootKey:
        return "Root";
    case CameraKey:
        return "Camera";
    case PlayerKey:
        return "Entity";
    case PlayerSpriteKey:
        return "Component";
    case PlayerTransformKey:
        return "Component";
    case LightsKey:
        return "Component";
    case TileMapKey:
        return "Asset";
    default:
        return "Unknown";
    }
}

[[nodiscard]] std::string_view hierarchyReadOnlyNote(UI::UITreeViewItemKey key) noexcept
{
    switch (key) {
    case SceneRootKey:
        return "Mock hierarchy only; no authoring write path.";
    case CameraKey:
        return "Viewport is a placeholder panel, not a live world view.";
    case PlayerKey:
        return "Selection drives Inspector labels only.";
    case PlayerSpriteKey:
        return "Weak AssetHandle display is not wired in this shell.";
    case PlayerTransformKey:
        return "No gizmo or property mutation.";
    case LightsKey:
        return "Lighting preview stays in product-2d, not this shell.";
    case TileMapKey:
        return "TileMap cook/edit is Deferred 2D-EDITOR work.";
    default:
        return "Read-only shell.";
    }
}

class EditorShellState final : public Tina::IGameState {
  public:
    EditorShellState(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_.stateEnters;

        auto rootBuilder = context.primaryWindowUIRootBuilder();
        if (!rootBuilder) {
            return Tina::Core::failure(std::move(rootBuilder.error()));
        }

        // Startup-only StyleClass/ColorToken/sheet must run before createRoot().
        auto dockClass = rootBuilder->registerStyleClass();
        if (!dockClass) {
            return Tina::Core::failure(std::move(dockClass.error()));
        }
        auto viewportClass = rootBuilder->registerStyleClass();
        if (!viewportClass) {
            return Tina::Core::failure(std::move(viewportClass.error()));
        }
        auto dockToken = rootBuilder->registerStyleColorToken(UI::rgb(0x152232));
        if (!dockToken) {
            return Tina::Core::failure(std::move(dockToken.error()));
        }
        auto viewportToken = rootBuilder->registerStyleColorToken(UI::rgb(0x0C141E));
        if (!viewportToken) {
            return Tina::Core::failure(std::move(viewportToken.error()));
        }
        dockClass_ = *dockClass;
        viewportClass_ = *viewportClass;
        viewportToken_ = *viewportToken;

        const std::array rules{
            UI::UIStyleBoxFillRule{
                .role = UI::UIStyleRoleId::PanelSurface,
                .styleClass = dockClass_,
                .colorToken = *dockToken,
            },
            UI::UIStyleBoxFillRule{
                .role = UI::UIStyleRoleId::PanelElevated,
                .styleClass = viewportClass_,
                .colorToken = viewportToken_,
            },
        };
        if (auto status = rootBuilder->installStyleSheet(std::span(rules)); !status) {
            return status;
        }
        counters_.stylesheetInstalled = true;
        counters_.styleRegisteredClasses = 2;
        counters_.styleRegisteredTokens = 2;
        counters_.styleActiveRules = rules.size();
        counters_.styleRevision = 1;

        auto root = rootBuilder->createRoot();
        if (!root) {
            return Tina::Core::failure(std::move(root.error()));
        }
        auto tree = rootBuilder->treeUpdater(*root);
        if (!tree) {
            return Tina::Core::failure(std::move(tree.error()));
        }

        if (auto status = tree->setProductTheme(UI::makeDefaultProductTheme()); !status) {
            return status;
        }

        UI::UILayoutStyle rootStyle = percentSize(100.0F, 100.0F);
        rootStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        rootStyle.flexContainer.gap = UI::UILayoutGap::All(8.0F);
        rootStyle.padding = {
            .left = 12.0F,
            .top = 12.0F,
            .right = 12.0F,
            .bottom = 12.0F,
        };
        if (auto status = tree->setLayoutStyle(root->rootNodeId(), rootStyle); !status) {
            return status;
        }

        auto title = tree->createElement(root->rootNodeId(),
                                         UI::makeLabelElement("Tina Editor Shell (read-only)"));
        if (!title) {
            return Tina::Core::failure(std::move(title.error()));
        }
        UI::UILayoutStyle titleStyle{};
        titleStyle.size.width = UI::UILayoutLength::Percent(100.0F);
        titleStyle.size.height = UI::UILayoutLength::Px(28.0F);
        if (auto status = tree->setLayoutStyle(*title, titleStyle); !status) {
            return status;
        }

        auto subtitle = tree->createElement(
            root->rootNodeId(),
            UI::makeLabelElement(
                "Not 2D-EDITOR. Mock hierarchy + startup StyleClass/token sheet + runtime token demo."));
        if (!subtitle) {
            return Tina::Core::failure(std::move(subtitle.error()));
        }
        UI::UILayoutStyle subtitleStyle{};
        subtitleStyle.size.width = UI::UILayoutLength::Percent(100.0F);
        subtitleStyle.size.height = UI::UILayoutLength::Px(22.0F);
        if (auto status = tree->setLayoutStyle(*subtitle, subtitleStyle); !status) {
            return status;
        }

        UI::UIElementDescriptor bodyDesc = UI::makePanelElement();
        bodyDesc.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
        bodyDesc.visual.styleClasses = std::span(&dockClass_, 1);
        auto body = tree->createElement(root->rootNodeId(), bodyDesc);
        if (!body) {
            return Tina::Core::failure(std::move(body.error()));
        }
        UI::UILayoutStyle bodyStyle = percentSize(100.0F, 100.0F);
        bodyStyle.flexItem.grow = 1.0F;
        bodyStyle.flexItem.shrink = 1.0F;
        bodyStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        bodyStyle.flexContainer.gap.column = 10.0F;
        if (auto status = tree->setLayoutStyle(*body, bodyStyle); !status) {
            return status;
        }

        UI::UIElementDescriptor dockDesc = UI::makePanelElement();
        dockDesc.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
        dockDesc.visual.styleClasses = std::span(&dockClass_, 1);
        auto left = tree->createElement(*body, dockDesc);
        if (!left) {
            return Tina::Core::failure(std::move(left.error()));
        }
        UI::UILayoutStyle leftStyle = flexChild(0.0F, 28.0F, 100.0F);
        leftStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        leftStyle.flexContainer.gap.row = 6.0F;
        if (auto status = tree->setLayoutStyle(*left, leftStyle); !status) {
            return status;
        }
        auto hierarchyTitle =
            tree->createElement(*left, UI::makeLabelElement("Hierarchy"));
        if (!hierarchyTitle) {
            return Tina::Core::failure(std::move(hierarchyTitle.error()));
        }
        UI::UILayoutStyle hierarchyTitleStyle{};
        hierarchyTitleStyle.size.width = UI::UILayoutLength::Percent(100.0F);
        hierarchyTitleStyle.size.height = UI::UILayoutLength::Px(22.0F);
        if (auto status = tree->setLayoutStyle(*hierarchyTitle, hierarchyTitleStyle); !status) {
            return status;
        }

        auto hierarchy = tree->createElement(
            *left, UI::makeTreeViewElement({.materializedItemCapacity = HierarchyMaterializedCapacity}));
        if (!hierarchy) {
            return Tina::Core::failure(std::move(hierarchy.error()));
        }
        hierarchyTree_ = *hierarchy;
        UI::UILayoutStyle hierarchyStyle = percentSize(100.0F, 100.0F);
        hierarchyStyle.flexItem.grow = 1.0F;
        hierarchyStyle.flexItem.shrink = 1.0F;
        if (auto status = tree->setLayoutStyle(*hierarchy, hierarchyStyle); !status) {
            return status;
        }
        if (auto status = tree->setTreeViewStyle(
                *hierarchy,
                UI::UITreeViewStyle{
                    .rowHeight = 26.0F,
                    .overscanRows = 1,
                    .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                    .wheelStep = 26.0F,
                    .indentation = 16.0F,
                    .disclosureExtent = 10.0F,
                    .disclosureGap = 4.0F,
                });
            !status) {
            return status;
        }
        if (auto status = tree->setTreeViewPaint(*hierarchy, UI::makeTreeViewPaint(UI::makeDefaultProductTheme()));
            !status) {
            return status;
        }
        if (auto status = tree->setTreeViewDataSource(*hierarchy, hierarchyDataSource()); !status) {
            return status;
        }
        if (auto status = tree->setTreeViewSelectedIndex(*hierarchy, 0); !status) {
            return status;
        }

        UI::UIElementDescriptor centerDesc = UI::makePanelElement();
        centerDesc.visual.styleRole = UI::UIStyleRoleId::PanelElevated;
        centerDesc.visual.styleClasses = std::span(&viewportClass_, 1);
        auto center = tree->createElement(*body, centerDesc);
        if (!center) {
            return Tina::Core::failure(std::move(center.error()));
        }
        UI::UILayoutStyle centerStyle = flexChild(1.0F, 44.0F, 100.0F);
        centerStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        centerStyle.flexContainer.gap.row = 6.0F;
        if (auto status = tree->setLayoutStyle(*center, centerStyle); !status) {
            return status;
        }
        auto viewportTitle =
            tree->createElement(*center, UI::makeLabelElement("Viewport (placeholder)"));
        if (!viewportTitle) {
            return Tina::Core::failure(std::move(viewportTitle.error()));
        }
        UI::UILayoutStyle viewportTitleStyle{};
        viewportTitleStyle.size.width = UI::UILayoutLength::Percent(100.0F);
        viewportTitleStyle.size.height = UI::UILayoutLength::Px(22.0F);
        if (auto status = tree->setLayoutStyle(*viewportTitle, viewportTitleStyle); !status) {
            return status;
        }
        UI::UIElementDescriptor viewportDesc = UI::makePanelElement();
        viewportDesc.visual.styleRole = UI::UIStyleRoleId::PanelElevated;
        viewportDesc.visual.styleClasses = std::span(&viewportClass_, 1);
        auto viewport = tree->createElement(*center, viewportDesc);
        if (!viewport) {
            return Tina::Core::failure(std::move(viewport.error()));
        }
        UI::UILayoutStyle viewportStyle = percentSize(100.0F, 100.0F);
        viewportStyle.flexItem.grow = 1.0F;
        viewportStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        viewportStyle.flexContainer.gap.row = 8.0F;
        viewportStyle.padding = {
            .left = 16.0F,
            .top = 16.0F,
            .right = 16.0F,
            .bottom = 16.0F,
        };
        if (auto status = tree->setLayoutStyle(*viewport, viewportStyle); !status) {
            return status;
        }
        // Multiple labels so the empty center is not a silent black void in screenshots.
        const std::array viewportHintTexts{
            std::string_view{"Viewport placeholder (read-only shell)"},
            std::string_view{"No live Scene / TileMap / Camera bind"},
            std::string_view{"Panel fill from stylesheet ColorToken"},
            std::string_view{"Not 2D-EDITOR product tooling"},
        };
        for (const std::string_view hintText : viewportHintTexts) {
            auto viewportHint = tree->createElement(*viewport, UI::makeLabelElement(hintText));
            if (!viewportHint) {
                return Tina::Core::failure(std::move(viewportHint.error()));
            }
            UI::UILayoutStyle viewportHintStyle{};
            viewportHintStyle.size.width = UI::UILayoutLength::Percent(100.0F);
            viewportHintStyle.size.height = UI::UILayoutLength::Px(22.0F);
            if (auto status = tree->setLayoutStyle(*viewportHint, viewportHintStyle); !status) {
                return status;
            }
        }

        UI::UIElementDescriptor rightDesc = UI::makePanelElement();
        rightDesc.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
        rightDesc.visual.styleClasses = std::span(&dockClass_, 1);
        auto right = tree->createElement(*body, rightDesc);
        if (!right) {
            return Tina::Core::failure(std::move(right.error()));
        }
        UI::UILayoutStyle rightStyle = flexChild(0.0F, 28.0F, 100.0F);
        rightStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        rightStyle.flexContainer.gap.row = 8.0F;
        if (auto status = tree->setLayoutStyle(*right, rightStyle); !status) {
            return status;
        }
        auto inspectorTitle =
            tree->createElement(*right, UI::makeLabelElement("Inspector"));
        if (!inspectorTitle) {
            return Tina::Core::failure(std::move(inspectorTitle.error()));
        }
        UI::UILayoutStyle inspectorTitleStyle{};
        inspectorTitleStyle.size.width = UI::UILayoutLength::Percent(100.0F);
        inspectorTitleStyle.size.height = UI::UILayoutLength::Px(22.0F);
        if (auto status = tree->setLayoutStyle(*inspectorTitle, inspectorTitleStyle); !status) {
            return status;
        }

        auto nameLabel = tree->createElement(*right, UI::makeLabelElement());
        if (!nameLabel) {
            return Tina::Core::failure(std::move(nameLabel.error()));
        }
        inspectorName_ = *nameLabel;
        auto kindLabel = tree->createElement(*right, UI::makeLabelElement());
        if (!kindLabel) {
            return Tina::Core::failure(std::move(kindLabel.error()));
        }
        inspectorKind_ = *kindLabel;
        auto noteLabel = tree->createElement(*right, UI::makeLabelElement());
        if (!noteLabel) {
            return Tina::Core::failure(std::move(noteLabel.error()));
        }
        inspectorNote_ = *noteLabel;
        for (const UI::UINodeId node : {inspectorName_, inspectorKind_, inspectorNote_}) {
            UI::UILayoutStyle labelStyle{};
            labelStyle.size.width = UI::UILayoutLength::Percent(100.0F);
            labelStyle.size.height = UI::UILayoutLength::Px(22.0F);
            if (auto status = tree->setLayoutStyle(node, labelStyle); !status) {
                return status;
            }
        }

        auto initialSelection = tree->treeViewSelection(hierarchyTree_);
        if (!initialSelection) {
            return Tina::Core::failure(std::move(initialSelection.error()));
        }
        selectionKey_ = initialSelection->key;
        if (auto status = publishInspector(*tree, selectionKey_); !status) {
            return status;
        }
        counters_.finalSelectionKey = selectionKey_;
        counters_.finalSelectionIndex = initialSelection->logicalIndex;
        counters_.hierarchyLogicalItems = hierarchyItemCount(this);
        counters_.selectionVerified = true;

        uiRoot_ = std::move(*root);
        ++counters_.uiRootsCreated;
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        if (uiRoot_) {
            uiRoot_.reset();
            ++counters_.uiRootsReleased;
        }
        ++counters_.stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_.frameUpdates;
        if (options_.autoSelectDemo) {
            const u64 first = (std::max)(u64{1}, options_.targetFrameCount / u64{3});
            const u64 second =
                (std::max)(first + u64{1}, options_.targetFrameCount - options_.targetFrameCount / u64{3});
            if (!queuedFirstSelection_ && counters_.frameUpdates >= first) {
                pendingSelectionIndex_ = 3;
                pendingViewportTokenColor_ = UI::rgb(0x1A3348);
                queuedFirstSelection_ = true;
            } else if (queuedFirstSelection_ && !queuedSecondSelection_ &&
                       counters_.frameUpdates >= second) {
                pendingSelectionIndex_ = 6;
                pendingViewportTokenColor_ = UI::rgb(0x0C141E);
                queuedSecondSelection_ = true;
            }
        }
        if (counters_.frameUpdates >= options_.targetFrameCount) {
            context.requestExitAfterFrame();
        }
        if (options_.frameDelayMilliseconds != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{options_.frameDelayMilliseconds});
        }
        return Tina::Core::success();
    }

    Tina::Core::Status updateUI(Tina::UIUpdateContext& context) override
    {
        if (!context.hasPrimaryWindowUI() || !uiRoot_) {
            return Tina::Core::success();
        }
        auto tree = context.primaryWindowUITreeUpdater(uiRoot_);
        if (!tree) {
            return Tina::Core::failure(std::move(tree.error()));
        }
        if (pendingViewportTokenColor_.has_value()) {
            const UI::UIStraightSrgba8Color color = *pendingViewportTokenColor_;
            pendingViewportTokenColor_.reset();
            if (auto status = tree->setStyleColorToken(viewportToken_, color); !status) {
                return status;
            }
            ++counters_.styleTokenUpdates;
        }
        if (pendingSelectionIndex_.has_value()) {
            const u64 index = *pendingSelectionIndex_;
            pendingSelectionIndex_.reset();
            if (auto status = tree->setTreeViewSelectedIndex(hierarchyTree_, index); !status) {
                return status;
            }
        }
        auto selection = tree->treeViewSelection(hierarchyTree_);
        if (!selection) {
            return Tina::Core::failure(std::move(selection.error()));
        }
        if (selection->key != selectionKey_) {
            selectionKey_ = selection->key;
            ++counters_.hierarchySelectionChanges;
            if (auto status = publishInspector(*tree, selectionKey_); !status) {
                return status;
            }
        }
        counters_.finalSelectionKey = selection->key;
        counters_.finalSelectionIndex = selection->logicalIndex;
        UI::UITreeViewItemDescriptor descriptor{};
        counters_.selectionVerified =
            resolveHierarchyItem(this, selection->logicalIndex, descriptor) &&
            descriptor.key == selection->key;
        auto metrics = tree->treeViewMetrics(hierarchyTree_);
        if (!metrics) {
            return Tina::Core::failure(std::move(metrics.error()));
        }
        counters_.hierarchyLogicalItems = metrics->logicalItemCount;
        return Tina::Core::success();
    }

  private:
    [[nodiscard]] Tina::Core::Status publishInspector(Tina::PrimaryWindowUITreeUpdater& tree,
                                                      UI::UITreeViewItemKey key)
    {
        std::string name = "Name: ";
        name += hierarchyLabel(key);
        if (auto status = tree.setText(inspectorName_, name); !status) {
            return status;
        }
        std::string kind = "Kind: ";
        kind += hierarchyKind(key);
        if (auto status = tree.setText(inspectorKind_, kind); !status) {
            return status;
        }
        std::string note = "Note: ";
        note += hierarchyReadOnlyNote(key);
        return tree.setText(inspectorNote_, note);
    }

    [[nodiscard]] UI::UITreeViewDataSource hierarchyDataSource() noexcept
    {
        return UI::UITreeViewDataSource{
            .state = this,
            .itemCount = &EditorShellState::hierarchyItemCount,
            .resolveItem = &EditorShellState::resolveHierarchyItem,
            .setItemExpanded = &EditorShellState::setHierarchyExpanded,
        };
    }

    [[nodiscard]] static u64 hierarchyItemCount(const void* state) noexcept
    {
        const auto* self = static_cast<const EditorShellState*>(state);
        u64 count = 1;
        if (!self->sceneExpanded_) {
            return count;
        }
        count += 2;
        if (self->playerExpanded_) {
            count += 2;
        }
        count += 2;
        return count;
    }

    static bool resolveHierarchyItem(const void* state, u64 logicalIndex,
                                     UI::UITreeViewItemDescriptor& output) noexcept
    {
        const auto* self = static_cast<const EditorShellState*>(state);
        u64 cursor = 0;
        const auto emit = [&](UI::UITreeViewItemKey key, u32 level, bool expandable, bool expanded) noexcept {
            if (cursor++ != logicalIndex) {
                return false;
            }
            output = UI::UITreeViewItemDescriptor{
                .key = key,
                .label = hierarchyLabel(key),
                .level = level,
                .enabled = true,
                .expandable = expandable,
                .expanded = expanded,
            };
            return true;
        };
        if (emit(SceneRootKey, 0, true, self->sceneExpanded_)) {
            return true;
        }
        if (!self->sceneExpanded_) {
            return false;
        }
        if (emit(CameraKey, 1, false, false)) {
            return true;
        }
        if (emit(PlayerKey, 1, true, self->playerExpanded_)) {
            return true;
        }
        if (self->playerExpanded_) {
            if (emit(PlayerSpriteKey, 2, false, false)) {
                return true;
            }
            if (emit(PlayerTransformKey, 2, false, false)) {
                return true;
            }
        }
        if (emit(LightsKey, 1, false, false)) {
            return true;
        }
        if (emit(TileMapKey, 1, false, false)) {
            return true;
        }
        return false;
    }

    static bool setHierarchyExpanded(void* state, UI::UITreeViewItemKey key, bool expanded) noexcept
    {
        auto* self = static_cast<EditorShellState*>(state);
        if (key == SceneRootKey) {
            self->sceneExpanded_ = expanded;
            if (!expanded) {
                self->playerExpanded_ = false;
            }
            return true;
        }
        if (key == PlayerKey) {
            if (!self->sceneExpanded_) {
                return false;
            }
            self->playerExpanded_ = expanded;
            return true;
        }
        return false;
    }

    SampleOptions options_;
    LifecycleCounters& counters_;
    Tina::UI::UIRootOwner uiRoot_{};
    UI::UINodeId hierarchyTree_{};
    UI::UINodeId inspectorName_{};
    UI::UINodeId inspectorKind_{};
    UI::UINodeId inspectorNote_{};
    UI::UIStyleClassId dockClass_{};
    UI::UIStyleClassId viewportClass_{};
    UI::UIStyleTokenId viewportToken_{};
    UI::UITreeViewItemKey selectionKey_ = UI::InvalidUITreeViewItemKey;
    bool sceneExpanded_ = true;
    bool playerExpanded_ = true;
    bool queuedFirstSelection_ = false;
    bool queuedSecondSelection_ = false;
    std::optional<u64> pendingSelectionIndex_{};
    std::optional<UI::UIStraightSrgba8Color> pendingViewportTokenColor_{};
};

class EditorShellApplication final : public Tina::IGameApplication {
  public:
    EditorShellApplication(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(counters)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        std::unique_ptr<Tina::IGameState> state = std::make_unique<EditorShellState>(options_, counters_);
        return state;
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++counters_.applicationShutdowns;
    }

  private:
    SampleOptions options_;
    LifecycleCounters& counters_;
};

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Editor Shell";
    config.primaryWindow.title = "Tina Editor Shell (read-only)";
    config.primaryWindow.initialLogicalExtent = {WindowLogicalWidth, WindowLogicalHeight};
    config.primaryWindow.initiallyVisible = true;
    return config;
}

[[nodiscard]] std::string runExitReasonName(Tina::RunExitReason exitReason)
{
    switch (exitReason) {
    case Tina::RunExitReason::GameRequestedExitAfterCurrentFrame:
        return "GameRequestedExitAfterCurrentFrame";
    case Tina::RunExitReason::PrimaryWindowRequestedClose:
        return "PrimaryWindowRequestedClose";
    case Tina::RunExitReason::GameStateStackBecameEmpty:
        return "GameStateStackBecameEmpty";
    }
    return "Unknown";
}

[[nodiscard]] Tina::Core::Status verifyLifecycle(Tina::RunExitReason exitReason, const SampleOptions& options,
                                                 const LifecycleCounters& counters)
{
    if (exitReason != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "editor shell stopped for an unexpected reason");
    }
    if (counters.frameUpdates != options.targetFrameCount || counters.stateEnters != 1 ||
        counters.stateExits != 1 || counters.applicationShutdowns != 1 || counters.uiRootsCreated != 1 ||
        counters.uiRootsReleased != 1 || !counters.selectionVerified || counters.hierarchyLogicalItems == 0 ||
        !counters.stylesheetInstalled || counters.styleRegisteredClasses != 2 ||
        counters.styleRegisteredTokens != 2 || counters.styleActiveRules != 2) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "editor shell lifecycle counters did not match contract");
    }
    if (options.autoSelectDemo) {
        if (counters.hierarchySelectionChanges < 1 || counters.finalSelectionKey != TileMapKey ||
            counters.styleTokenUpdates < 2) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor shell auto-select/token demo did not finish");
        }
    }
    return Tina::Core::success();
}

[[nodiscard]] int runSample(int argumentCount, char** arguments)
{
    auto optionsResult = parseOptions(argumentCount, arguments);
    if (!optionsResult) {
        writeError(optionsResult.error());
        return 2;
    }
    const SampleOptions options = *optionsResult;

    auto hostResult = Tina::Desktop::CreateEngine(createEngineConfig());
    if (!hostResult) {
        writeError(hostResult.error());
        return 1;
    }

    LifecycleCounters counters{};
    EditorShellApplication application{options, counters};
    auto runResult = (*hostResult)->run(application);
    if (!runResult) {
        writeError(runResult.error());
        return 1;
    }

    auto lifecycleStatus = verifyLifecycle(*runResult, options, counters);
    if (!lifecycleStatus) {
        writeError(lifecycleStatus.error());
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_editor_shell\",\"readOnly\":true"
              << ",\"editorModule\":false,\"stylesheetInstalled\":"
              << (counters.stylesheetInstalled ? "true" : "false") << ",\"frames\":" << counters.frameUpdates
              << ",\"targetFrames\":" << options.targetFrameCount
              << ",\"frameDelayMs\":" << options.frameDelayMilliseconds << ",\"exit\":";
    writeJsonString(std::cout, runExitReasonName(*runResult));
    std::cout << ",\"stateEnters\":" << counters.stateEnters << ",\"stateExits\":" << counters.stateExits
              << ",\"applicationShutdowns\":" << counters.applicationShutdowns
              << ",\"uiRootsCreated\":" << counters.uiRootsCreated
              << ",\"uiRootsReleased\":" << counters.uiRootsReleased
              << ",\"hierarchySelectionChanges\":" << counters.hierarchySelectionChanges
              << ",\"hierarchyLogicalItems\":" << counters.hierarchyLogicalItems
              << ",\"styleRegisteredClasses\":" << counters.styleRegisteredClasses
              << ",\"styleRegisteredTokens\":" << counters.styleRegisteredTokens
              << ",\"styleActiveRules\":" << counters.styleActiveRules
              << ",\"styleRevision\":" << counters.styleRevision
              << ",\"styleTokenUpdates\":" << counters.styleTokenUpdates
              << ",\"finalSelectionKey\":" << counters.finalSelectionKey
              << ",\"finalSelectionIndex\":" << counters.finalSelectionIndex
              << ",\"selectionVerified\":" << (counters.selectionVerified ? "true" : "false") << "}\n";
    return 0;
}

} // namespace

int main(int argumentCount, char** arguments)
{
    try {
        return runSample(argumentCount, arguments);
    } catch (const std::bad_alloc&) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory,
                                "The editor shell sample ran out of memory"};
        writeError(error);
        return 1;
    } catch (const std::exception& exception) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the editor shell sample boundary"};
        error.addContext("tina_sample_editor_shell", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "A non-standard exception crossed the editor shell sample boundary"};
        writeError(error);
        return 1;
    }
}
