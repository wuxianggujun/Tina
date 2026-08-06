// 2D editor shell: retained tool chrome backed by a validated World2D
// authoring document, bounded undo/redo, and Scene runtime preview.

#include <tina/asset_format/World2DSnapshot.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/editor/World2DAuthoringDocument.hpp>
#include <tina/editor/World2DAuthoringFile.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/scene/World.hpp>
#include <tina/scene/World2DSnapshot.hpp>
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
#include <cmath>
#include <cstddef>
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
#include <vector>

#include "../common/SampleSpriteFrameResource.hpp"

namespace {

namespace UI = Tina::UI;

using Tina::Core::u8;
using Tina::Core::u32;
using Tina::Core::u64;

inline constexpr u64 DefaultFrameCount = 60;
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;
inline constexpr u32 WindowLogicalWidth = 1280;
inline constexpr u32 WindowLogicalHeight = 800;
inline constexpr u32 HierarchyMaterializedCapacity = 16;
inline constexpr u32 AuthoringEntityCapacity = 16;
inline constexpr u32 InitialAuthoringEntityCount = 5;
inline constexpr u32 EditorActionCount = 5;
inline constexpr u32 EditorLayoutRegionCount = 6;
inline constexpr u32 GpuViewportSpriteCount = 4;
inline constexpr float PreviewWorldWidth = 16.0F;
inline constexpr float PreviewWorldHeight = 9.0F;
inline constexpr float DegreesToRadians = 0.01745329251994329577F;
inline constexpr float RadiansToDegrees = 57.295779513082320876F;

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
    std::string documentPathUtf8{};
    bool autoDemo = true;
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
    u64 renderExtractions = 0;
    u64 gpuViewportSprites = 0;
    u64 gpuViewportDocumentRevision = 0;
    u64 styleRegisteredClasses = 0;
    u64 styleRegisteredTokens = 0;
    u64 styleActiveRules = 0;
    u64 styleRevision = 0;
    u64 styleTokenUpdates = 0;
    u64 authoringActionsWired = 0;
    u64 authoringEdits = 0;
    u64 authoringUndos = 0;
    u64 authoringRedos = 0;
    u64 authoringSaves = 0;
    u64 inspectorTransactions = 0;
    u64 inspectorRejectedTransactions = 0;
    u64 savedSnapshotBytes = 0;
    u64 runtimePreviewInstantiations = 0;
    u64 documentRevision = 0;
    u64 documentEntityCount = 0;
    u64 documentUndoDepth = 0;
    u64 documentRedoDepth = 0;
    u64 cookPreviewBytes = 0;
    float finalPlayerPositionX = 0.0F;
    float finalPlayerPositionY = 0.0F;
    float finalPlayerRotationDegrees = 0.0F;
    float finalPlayerScaleX = 1.0F;
    float finalPlayerScaleY = 1.0F;
    float viewportLogicalX = 0.0F;
    float viewportLogicalY = 0.0F;
    float viewportLogicalWidth = 0.0F;
    float viewportLogicalHeight = 0.0F;
    float viewportNormalizedX = 0.0F;
    float viewportNormalizedY = 0.0F;
    float viewportNormalizedWidth = 0.0F;
    float viewportNormalizedHeight = 0.0F;
    u64 editorLayoutRegions = 0;
    bool selectionVerified = false;
    bool stylesheetInstalled = false;
    bool runtimePreviewValid = false;
    bool gpuViewportReady = false;
    bool viewportLayoutReady = false;
    bool inspectorScrollConfigured = false;
    bool documentPathConfigured = false;
    bool documentLoaded = false;
    bool documentSaved = false;
    bool documentDirty = true;
};

enum class EditorCommand : u32 {
    MoveSelectedPositiveX,
    ApplyTransform,
    Undo,
    Redo,
    Save,
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

[[nodiscard]] bool parseFiniteFloat(std::string_view text, float& value) noexcept
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() && std::isfinite(value);
}

[[nodiscard]] float planarRotationDegrees(float rotationX, float rotationY, float rotationZ,
                                          float rotationW) noexcept
{
    const double lengthSquared = static_cast<double>(rotationX) * rotationX +
                                 static_cast<double>(rotationY) * rotationY +
                                 static_cast<double>(rotationZ) * rotationZ +
                                 static_cast<double>(rotationW) * rotationW;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12) {
        return 0.0F;
    }
    const float inverseLength = static_cast<float>(1.0 / std::sqrt(lengthSquared));
    rotationX *= inverseLength;
    rotationY *= inverseLength;
    rotationZ *= inverseLength;
    rotationW *= inverseLength;
    const float sinAngle = 2.0F * (rotationW * rotationZ + rotationX * rotationY);
    const float cosAngle = 1.0F - 2.0F * (rotationY * rotationY + rotationZ * rotationZ);
    return std::atan2(sinAngle, cosAngle) * RadiansToDegrees;
}

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argumentCount, char** arguments)
{
    constexpr std::string_view FramesPrefix = "--frames=";
    constexpr std::string_view DelayPrefix = "--frame-delay-ms=";
    constexpr std::string_view DocumentPathPrefix = "--document-path=";

    SampleOptions options{};
    bool hasFrames = false;
    bool hasDelay = false;
    bool hasDocumentPath = false;
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
        if (argument.starts_with(DocumentPathPrefix)) {
            if (hasDocumentPath) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --document-path argument");
            }
            const std::string_view value = argument.substr(DocumentPathPrefix.size());
            if (value.empty()) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--document-path must not be empty");
            }
            options.documentPathUtf8.assign(value);
            hasDocumentPath = true;
            continue;
        }
        if (argument == "--no-auto-demo") {
            options.autoDemo = false;
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

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] UI::UILayoutStyle fillWidth(float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Percent(100.0F);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] UI::UILayoutStyle growingRegion() noexcept
{
    UI::UILayoutStyle style = percentSize(100.0F, 100.0F);
    style.flexItem.grow = 1.0F;
    style.flexItem.shrink = 1.0F;
    style.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    return style;
}

[[nodiscard]] UI::UILayoutStyle boundedDock(float basisPercent, float minimumWidth,
                                            float maximumWidth) noexcept
{
    UI::UILayoutStyle style = flexChild(0.0F, basisPercent, 100.0F);
    style.minMax.minWidth = UI::UILayoutLength::Px(minimumWidth);
    style.minMax.maxWidth = UI::UILayoutLength::Px(maximumWidth);
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

[[nodiscard]] std::string_view hierarchyAuthoringNote(UI::UITreeViewItemKey key) noexcept
{
    switch (key) {
    case SceneRootKey:
        return "Canonical World2D root.";
    case CameraKey:
        return "Persisted Camera2D entity.";
    case PlayerKey:
        return "Stable entity identity.";
    case PlayerSpriteKey:
        return "Player sprite component.";
    case PlayerTransformKey:
        return "Move X is undoable.";
    case LightsKey:
        return "Runtime PointLight2D.";
    case TileMapKey:
        return "TileMap authoring follows.";
    default:
        return "Validated World2D authoring item.";
    }
}

[[nodiscard]] u32 stableEntityIdForHierarchyItem(UI::UITreeViewItemKey key) noexcept
{
    switch (key) {
    case SceneRootKey:
        return 1;
    case CameraKey:
        return 2;
    case PlayerKey:
    case PlayerSpriteKey:
    case PlayerTransformKey:
        return 3;
    case LightsKey:
        return 6;
    case TileMapKey:
        return 7;
    default:
        return 0;
    }
}

struct InitialAuthoringDocument final {
    Tina::Editor::World2DAuthoringDocument document;
    bool loadedFromPath = false;
};

[[nodiscard]] Tina::Core::Result<InitialAuthoringDocument>
createAuthoringDocument(std::string_view documentPathUtf8)
{
    auto document = Tina::Editor::World2DAuthoringDocument::Create({
        .entityCapacity = AuthoringEntityCapacity,
        .gameplayByteCapacity = 1024,
        .historyEntryCapacity = 8,
        .historyByteCapacity = 64U * 1024U,
    });
    if (!document) {
        return Tina::Core::failure(std::move(document.error()));
    }

    if (!documentPathUtf8.empty()) {
        auto status = Tina::Editor::loadWorld2DAuthoringDocument(documentPathUtf8, *document);
        if (status) {
            return InitialAuthoringDocument{
                .document = std::move(*document),
                .loadedFromPath = true,
            };
        }
        if (status.error().code != Tina::Core::CoreErrorCode::NotFound) {
            return Tina::Core::failure(std::move(status.error()));
        }
    }

    const std::array entities{
        Tina::AssetFormat::World2DEntityDesc{.stableEntityId = 1},
        Tina::AssetFormat::World2DEntityDesc{
            .stableEntityId = 2,
            .parentStableEntityId = 1,
            .positionY = 4.0F,
            .camera = Tina::AssetFormat::World2DCameraDesc{},
        },
        Tina::AssetFormat::World2DEntityDesc{
            .stableEntityId = 3,
            .parentStableEntityId = 1,
        },
        Tina::AssetFormat::World2DEntityDesc{
            .stableEntityId = 6,
            .parentStableEntityId = 1,
            .positionX = 2.0F,
            .positionY = 2.0F,
            .pointLight = Tina::AssetFormat::World2DPointLightDesc{},
        },
        Tina::AssetFormat::World2DEntityDesc{
            .stableEntityId = 7,
            .parentStableEntityId = 1,
        },
    };
    auto bytes = Tina::AssetFormat::writeWorld2DSnapshotBytes(
        Tina::AssetFormat::World2DSnapshotDesc{.entities = entities});
    if (!bytes) {
        return Tina::Core::failure(std::move(bytes.error()));
    }
    if (auto status = document->loadSnapshot(*bytes); !status) {
        return Tina::Core::failure(std::move(status.error()));
    }
    return InitialAuthoringDocument{.document = std::move(*document)};
}

class EditorShellState final : public Tina::IGameState {
  public:
    EditorShellState(SampleOptions options, LifecycleCounters& counters,
                     Tina::Editor::World2DAuthoringDocument document,
                     std::vector<std::byte> savedBaselineBytes, bool documentLoaded) noexcept
        : options_(options), counters_(counters), document_(std::move(document)),
          documentLoaded_(documentLoaded), savedBaselineBytes_(std::move(savedBaselineBytes))
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

        const UI::UITheme productTheme = UI::makeDefaultProductTheme();
        if (auto status = tree->setProductTheme(productTheme); !status) {
            return status;
        }

        const UI::UITextStyle titleText = UI::makeTitleTextStyle(productTheme, 18.0F);
        const UI::UITextStyle sectionText = UI::makeTitleTextStyle(productTheme, 15.0F);
        const UI::UITextStyle bodyText = UI::makeBodyTextStyle(productTheme, 14.0F);
        const UI::UITextStyle compactText = UI::makeBodyTextStyle(productTheme, 12.0F);
        const UI::UITextStyle secondaryText = UI::makeSecondaryTextStyle(productTheme, 12.0F);
        const UI::UITextStyle accentText = UI::makeAccentTextStyle(productTheme, 13.0F);

        const auto storeNode = [](auto&& result, UI::UINodeId& output) -> Tina::Core::Status {
            if (!result) {
                return Tina::Core::failure(std::move(result.error()));
            }
            output = *result;
            return Tina::Core::success();
        };
        const auto createPanel = [&](UI::UINodeId parent, UI::UILayoutStyle layout,
                                     UI::UIStyleRoleId role,
                                     UI::UIStyleClassId styleClass = {}) {
            UI::UIElementDescriptor descriptor = UI::makePanelElement(layout);
            descriptor.visual.styleRole = role;
            if (styleClass.hasValue()) {
                descriptor.visual.styleClasses = std::span(&styleClass, 1);
            }
            return tree->createElement(parent, descriptor);
        };
        const auto createLabel = [&](UI::UINodeId parent, std::string_view text,
                                     UI::UILayoutStyle layout, const UI::UITextStyle& style) {
            UI::UIElementDescriptor descriptor = UI::makeLabelElement(text, layout);
            descriptor.textStyle = style;
            return tree->createElement(parent, descriptor);
        };
        const auto createButton = [&](UI::UINodeId parent, std::string_view text,
                                      UI::UILayoutStyle layout, bool enabled = true) {
            UI::UIElementDescriptor descriptor = UI::makeButtonElement(text, layout);
            descriptor.textStyle = compactText;
            descriptor.enabled = enabled;
            return tree->createElement(parent, descriptor);
        };
        const auto createTextEdit = [&](UI::UINodeId parent, std::string_view text,
                                        UI::UILayoutStyle layout, bool enabled) {
            UI::UIElementDescriptor descriptor = UI::makeTextEditElement(text, layout);
            descriptor.textStyle = compactText;
            descriptor.enabled = enabled;
            return tree->createElement(parent, descriptor);
        };

        UI::UILayoutStyle rootStyle = percentSize(100.0F, 100.0F);
        rootStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        rootStyle.flexContainer.gap.row = 6.0F;
        rootStyle.padding = UI::UIEdgeSpacing::All(10.0F);
        if (auto status = tree->setLayoutStyle(root->rootNodeId(), rootStyle); !status) {
            return status;
        }

        const UI::UINodeId rootNode = root->rootNodeId();
        UI::UINodeId toolbar{};
        UI::UILayoutStyle toolbarStyle = fillWidth(48.0F);
        toolbarStyle.flexItem.shrink = 0.0F;
        toolbarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        toolbarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        toolbarStyle.flexContainer.gap.column = 8.0F;
        toolbarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(10.0F, 7.0F);
        if (auto status = storeNode(createPanel(rootNode, toolbarStyle, UI::UIStyleRoleId::PanelSurface,
                                                dockClass_),
                                    toolbar);
            !status) {
            return status;
        }

        UI::UINodeId toolbarBrand{};
        if (auto status = storeNode(createLabel(toolbar, "TINA EDITOR", fixedSize(132.0F, 26.0F), titleText),
                                    toolbarBrand);
            !status) {
            return status;
        }
        UI::UINodeId toolbarDocument{};
        if (auto status = storeNode(createLabel(toolbar, "World2D Scene", fixedSize(138.0F, 24.0F), bodyText),
                                    toolbarDocument);
            !status) {
            return status;
        }
        UI::UILayoutStyle pathStyle = fixedSize(0.0F, 22.0F);
        pathStyle.size.width = UI::UILayoutLength::Auto();
        pathStyle.flexItem.grow = 1.0F;
        pathStyle.flexItem.shrink = 1.0F;
        pathStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        const std::string_view initialPathStatus =
            options_.documentPathUtf8.empty()
                ? "No save path | Unsaved"
                : (documentLoaded_ ? "Existing document opened | Saved"
                                   : "Save target configured | Modified");
        if (auto status = storeNode(createLabel(toolbar, initialPathStatus, pathStyle, secondaryText),
                                    toolbarPath_);
            !status) {
            return status;
        }

        UI::UINodeId mode2DButton{};
        if (auto status = storeNode(createButton(toolbar, "2D", fixedSize(46.0F, 30.0F), false), mode2DButton);
            !status) {
            return status;
        }
        UI::UINodeId previewModeButton{};
        if (auto status = storeNode(createButton(toolbar, "Preview", fixedSize(68.0F, 30.0F), false),
                                    previewModeButton);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(toolbar, "Undo", fixedSize(60.0F, 30.0F)), undoButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(toolbar, "Redo", fixedSize(60.0F, 30.0F)), redoButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(toolbar, "Save", fixedSize(58.0F, 30.0F),
                                                 !options_.documentPathUtf8.empty()),
                                    saveButton_);
            !status) {
            return status;
        }

        UI::UINodeId contextBar{};
        UI::UILayoutStyle contextBarStyle = fillWidth(34.0F);
        contextBarStyle.flexItem.shrink = 0.0F;
        contextBarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        contextBarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        contextBarStyle.flexContainer.gap.column = 6.0F;
        contextBarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(10.0F, 3.0F);
        if (auto status = storeNode(createPanel(rootNode, contextBarStyle, UI::UIStyleRoleId::PanelElevated,
                                                viewportClass_),
                                    contextBar);
            !status) {
            return status;
        }
        UI::UILayoutStyle breadcrumbStyle = fixedSize(0.0F, 22.0F);
        breadcrumbStyle.size.width = UI::UILayoutLength::Auto();
        breadcrumbStyle.flexItem.grow = 1.0F;
        breadcrumbStyle.flexItem.shrink = 1.0F;
        breadcrumbStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        UI::UINodeId breadcrumb{};
        if (auto status = storeNode(createLabel(contextBar, "Scene / World2D / Player", breadcrumbStyle,
                                                secondaryText),
                                    breadcrumb);
            !status) {
            return status;
        }
        for (const std::string_view tool : {std::string_view{"Select"}, std::string_view{"Move"},
                                            std::string_view{"Frame"}}) {
            UI::UINodeId toolButton{};
            if (auto status = storeNode(createButton(contextBar, tool, fixedSize(58.0F, 26.0F), false),
                                        toolButton);
                !status) {
                return status;
            }
        }
        UI::UINodeId snapStatus{};
        if (auto status = storeNode(createLabel(contextBar, "Snap 16 px", fixedSize(72.0F, 20.0F), accentText),
                                    snapStatus);
            !status) {
            return status;
        }

        UI::UINodeId body{};
        UI::UILayoutStyle bodyStyle = growingRegion();
        bodyStyle.minMax.minHeight = UI::UILayoutLength::Px(320.0F);
        bodyStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        bodyStyle.flexContainer.gap.column = 8.0F;
        if (auto status = storeNode(createPanel(rootNode, bodyStyle, UI::UIStyleRoleId::None), body); !status) {
            return status;
        }

        UI::UINodeId left{};
        UI::UILayoutStyle leftStyle = boundedDock(22.0F, 220.0F, 300.0F);
        leftStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        leftStyle.flexContainer.gap.row = 7.0F;
        leftStyle.padding = UI::UIEdgeSpacing::All(8.0F);
        if (auto status = storeNode(createPanel(body, leftStyle, UI::UIStyleRoleId::PanelSurface, dockClass_), left);
            !status) {
            return status;
        }

        UI::UINodeId hierarchyHeader{};
        UI::UILayoutStyle hierarchyHeaderStyle = fillWidth(28.0F);
        hierarchyHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        hierarchyHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        if (auto status = storeNode(createPanel(left, hierarchyHeaderStyle, UI::UIStyleRoleId::None),
                                    hierarchyHeader);
            !status) {
            return status;
        }
        UI::UILayoutStyle hierarchyTitleStyle = fixedSize(0.0F, 24.0F);
        hierarchyTitleStyle.size.width = UI::UILayoutLength::Auto();
        hierarchyTitleStyle.flexItem.grow = 1.0F;
        hierarchyTitleStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        UI::UINodeId hierarchyTitle{};
        if (auto status = storeNode(createLabel(hierarchyHeader, "Hierarchy", hierarchyTitleStyle, sectionText),
                                    hierarchyTitle);
            !status) {
            return status;
        }
        UI::UINodeId hierarchyCount{};
        if (auto status = storeNode(createLabel(hierarchyHeader, "5 entities", fixedSize(66.0F, 20.0F),
                                                secondaryText),
                                    hierarchyCount);
            !status) {
            return status;
        }

        UI::UINodeId hierarchyFilter{};
        if (auto status = storeNode(createTextEdit(left, "Filter hierarchy", fillWidth(32.0F), false),
                                    hierarchyFilter);
            !status) {
            return status;
        }
        UI::UINodeId hierarchyActions{};
        UI::UILayoutStyle hierarchyActionsStyle = fillWidth(30.0F);
        hierarchyActionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        hierarchyActionsStyle.flexContainer.gap.column = 6.0F;
        if (auto status = storeNode(createPanel(left, hierarchyActionsStyle, UI::UIStyleRoleId::None),
                                    hierarchyActions);
            !status) {
            return status;
        }
        UI::UILayoutStyle hierarchyActionStyle = fixedSize(0.0F, 30.0F);
        hierarchyActionStyle.size.width = UI::UILayoutLength::Auto();
        hierarchyActionStyle.flexItem.grow = 1.0F;
        hierarchyActionStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        for (const std::string_view action : {std::string_view{"Add Entity"},
                                              std::string_view{"Focus"}}) {
            UI::UINodeId actionButton{};
            if (auto status = storeNode(createButton(hierarchyActions, action, hierarchyActionStyle, false),
                                        actionButton);
                !status) {
                return status;
            }
        }

        UI::UILayoutStyle hierarchyStyle = growingRegion();
        hierarchyStyle.minMax.minHeight = UI::UILayoutLength::Px(160.0F);
        auto hierarchy = tree->createElement(
            left, UI::makeTreeViewElement({.materializedItemCapacity = HierarchyMaterializedCapacity},
                                          hierarchyStyle));
        if (!hierarchy) {
            return Tina::Core::failure(std::move(hierarchy.error()));
        }
        hierarchyTree_ = *hierarchy;
        if (auto status = tree->setTreeViewStyle(
                hierarchyTree_,
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
        if (auto status = tree->setTreeViewPaint(hierarchyTree_, UI::makeTreeViewPaint(productTheme)); !status) {
            return status;
        }
        if (auto status = tree->setTreeViewDataSource(hierarchyTree_, hierarchyDataSource()); !status) {
            return status;
        }
        if (auto status = tree->setTreeViewSelectedIndex(hierarchyTree_, 0); !status) {
            return status;
        }

        UI::UINodeId hierarchyFooter{};
        UI::UILayoutStyle hierarchyFooterStyle = fillWidth(48.0F);
        hierarchyFooterStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(8.0F, 5.0F);
        hierarchyFooterStyle.flexContainer.gap.row = 2.0F;
        if (auto status = storeNode(createPanel(left, hierarchyFooterStyle, UI::UIStyleRoleId::PanelElevated),
                                    hierarchyFooter);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(hierarchyFooter, {}, fillWidth(20.0F), bodyText),
                                    hierarchySelectionSummary_);
            !status) {
            return status;
        }
        UI::UINodeId hierarchyFooterHint{};
        if (auto status = storeNode(createLabel(hierarchyFooter, "Stable authoring identity", fillWidth(18.0F),
                                                secondaryText),
                                    hierarchyFooterHint);
            !status) {
            return status;
        }

        UI::UINodeId center{};
        UI::UILayoutStyle centerStyle = growingRegion();
        centerStyle.size.width = UI::UILayoutLength::Percent(52.0F);
        centerStyle.minMax.minWidth = UI::UILayoutLength::Px(360.0F);
        centerStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        centerStyle.flexContainer.gap.row = 7.0F;
        centerStyle.padding = UI::UIEdgeSpacing::All(8.0F);
        if (auto status = storeNode(createPanel(body, centerStyle, UI::UIStyleRoleId::None), center);
            !status) {
            return status;
        }

        UI::UINodeId viewportHeader{};
        UI::UILayoutStyle viewportHeaderStyle = fillWidth(30.0F);
        viewportHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        viewportHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        viewportHeaderStyle.flexContainer.gap.column = 8.0F;
        if (auto status = storeNode(createPanel(center, viewportHeaderStyle, UI::UIStyleRoleId::None),
                                    viewportHeader);
            !status) {
            return status;
        }
        UI::UILayoutStyle viewportTitleStyle = fixedSize(0.0F, 24.0F);
        viewportTitleStyle.size.width = UI::UILayoutLength::Auto();
        viewportTitleStyle.flexItem.grow = 1.0F;
        viewportTitleStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        UI::UINodeId viewportTitle{};
        if (auto status = storeNode(createLabel(viewportHeader, "World2D Viewport", viewportTitleStyle,
                                                sectionText),
                                    viewportTitle);
            !status) {
            return status;
        }
        UI::UINodeId viewportMode{};
        UI::UIElementDescriptor viewportModeDesc =
            UI::makeDropdownElement("Orthographic", fixedSize(108.0F, 28.0F));
        viewportModeDesc.textStyle = compactText;
        viewportModeDesc.enabled = false;
        if (auto status = storeNode(tree->createElement(viewportHeader, viewportModeDesc), viewportMode); !status) {
            return status;
        }

        UI::UINodeId viewportTools{};
        UI::UILayoutStyle viewportToolsStyle = fillWidth(32.0F);
        viewportToolsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        viewportToolsStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        viewportToolsStyle.flexContainer.gap.column = 6.0F;
        if (auto status = storeNode(createPanel(center, viewportToolsStyle, UI::UIStyleRoleId::PanelSurface),
                                    viewportTools);
            !status) {
            return status;
        }
        for (const std::string_view tool : {std::string_view{"Select"}, std::string_view{"Move"},
                                            std::string_view{"Frame All"}}) {
            UI::UINodeId toolButton{};
            if (auto status = storeNode(createButton(viewportTools, tool, fixedSize(64.0F, 28.0F), false),
                                        toolButton);
                !status) {
                return status;
            }
        }
        UI::UILayoutStyle zoomSliderStyle = fixedSize(0.0F, 24.0F);
        zoomSliderStyle.size.width = UI::UILayoutLength::Auto();
        zoomSliderStyle.flexItem.grow = 1.0F;
        zoomSliderStyle.flexItem.shrink = 1.0F;
        zoomSliderStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        UI::UIElementDescriptor zoomSliderDesc = UI::makeSliderElement(zoomSliderStyle);
        zoomSliderDesc.enabled = false;
        UI::UINodeId zoomSlider{};
        if (auto status = storeNode(tree->createElement(viewportTools, zoomSliderDesc), zoomSlider); !status) {
            return status;
        }
        if (auto status = tree->setSliderRange(zoomSlider, 25.0F, 400.0F, 25.0F); !status) {
            return status;
        }
        if (auto status = tree->setSliderValue(zoomSlider, 100.0F); !status) {
            return status;
        }
        UI::UINodeId zoomValue{};
        if (auto status = storeNode(createLabel(viewportTools, "100%", fixedSize(42.0F, 20.0F), accentText),
                                    zoomValue);
            !status) {
            return status;
        }

        UI::UINodeId viewportCanvas{};
        UI::UILayoutStyle viewportCanvasStyle = growingRegion();
        viewportCanvasStyle.minMax.minHeight = UI::UILayoutLength::Px(220.0F);
        viewportCanvasStyle.padding = UI::UIEdgeSpacing::All(12.0F);
        viewportCanvasStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
        if (auto status = storeNode(createPanel(center, viewportCanvasStyle, UI::UIStyleRoleId::None),
                                    viewportCanvas);
            !status) {
            return status;
        }
        UI::UINodeId viewportCanvasTop{};
        UI::UILayoutStyle viewportCanvasTopStyle = fillWidth(22.0F);
        viewportCanvasTopStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        viewportCanvasTopStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
        if (auto status = storeNode(createPanel(viewportCanvas, viewportCanvasTopStyle, UI::UIStyleRoleId::None),
                                    viewportCanvasTop);
            !status) {
            return status;
        }
        UI::UINodeId gridStatus{};
        if (auto status = storeNode(createLabel(viewportCanvasTop, "Grid 16 px", fixedSize(76.0F, 20.0F),
                                                secondaryText),
                                    gridStatus);
            !status) {
            return status;
        }
        UI::UINodeId previewStatus{};
        if (auto status = storeNode(createLabel(viewportCanvasTop, "Runtime preview ready",
                                                fixedSize(132.0F, 20.0F), accentText),
                                    previewStatus);
            !status) {
            return status;
        }

        UI::UINodeId viewportSceneArea{};
        UI::UILayoutStyle viewportSceneAreaStyle = growingRegion();
        viewportSceneAreaStyle.flexContainer.justifyContent = UI::UIJustifyContent::Center;
        viewportSceneAreaStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        if (auto status = storeNode(createPanel(viewportCanvas, viewportSceneAreaStyle,
                                                UI::UIStyleRoleId::None),
                                    viewportSceneArea);
            !status) {
            return status;
        }
        UI::UINodeId previewFrame{};
        UI::UILayoutStyle previewFrameStyle = fixedSize(300.0F, 146.0F);
        previewFrameStyle = percentSize(86.0F, 82.0F);
        previewFrameStyle.minMax.minWidth = UI::UILayoutLength::Px(300.0F);
        previewFrameStyle.minMax.minHeight = UI::UILayoutLength::Px(146.0F);
        previewFrameStyle.minMax.maxWidth = UI::UILayoutLength::Px(600.0F);
        previewFrameStyle.minMax.maxHeight = UI::UILayoutLength::Px(360.0F);
        previewFrameStyle.padding = UI::UIEdgeSpacing::All(14.0F);
        previewFrameStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        previewFrameStyle.flexContainer.justifyContent = UI::UIJustifyContent::Center;
        previewFrameStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        previewFrameStyle.flexContainer.gap.row = 8.0F;
        if (auto status = storeNode(createPanel(viewportSceneArea, previewFrameStyle,
                                                UI::UIStyleRoleId::None),
                                    previewFrame);
            !status) {
            return status;
        }
        UI::UINodeId previewTitle{};
        if (auto status = storeNode(createLabel(previewFrame, "World2D Scene", fixedSize(170.0F, 24.0F),
                                                titleText),
                                    previewTitle);
            !status) {
            return status;
        }
        UI::UILayoutStyle previewWorldLayerStyle = growingRegion();
        previewWorldLayerStyle.minMax.minHeight = UI::UILayoutLength::Px(72.0F);
        previewWorldLayerStyle.placement = UI::UILayoutPlacement::Flow;
        if (auto status = storeNode(createPanel(previewFrame, previewWorldLayerStyle,
                                                UI::UIStyleRoleId::None),
                                    viewportPreviewLayer_);
            !status) {
            return status;
        }
        UI::UINodeId previewEntities{};
        if (auto status = storeNode(createLabel(previewFrame, "Camera | Player | Light | TileMap",
                                                fixedSize(240.0F, 22.0F), bodyText),
                                    previewEntities);
            !status) {
            return status;
        }
        UI::UINodeId previewCook{};
        if (auto status = storeNode(createLabel(previewFrame, "Canonical snapshot -> Scene::World",
                                                fixedSize(242.0F, 20.0F), secondaryText),
                                    previewCook);
            !status) {
            return status;
        }

        UI::UINodeId viewportCanvasBottom{};
        UI::UILayoutStyle viewportCanvasBottomStyle = fillWidth(22.0F);
        viewportCanvasBottomStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        viewportCanvasBottomStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
        if (auto status = storeNode(createPanel(viewportCanvas, viewportCanvasBottomStyle,
                                                UI::UIStyleRoleId::None),
                                    viewportCanvasBottom);
            !status) {
            return status;
        }
        UI::UINodeId viewportOrigin{};
        if (auto status = storeNode(createLabel(viewportCanvasBottom, "Origin 0, 0", fixedSize(76.0F, 20.0F),
                                                secondaryText),
                                    viewportOrigin);
            !status) {
            return status;
        }
        UI::UINodeId viewportExtent{};
        if (auto status = storeNode(createLabel(viewportCanvasBottom, "1280 x 800 logical",
                                                fixedSize(120.0F, 20.0F), secondaryText),
                                    viewportExtent);
            !status) {
            return status;
        }

        UI::UINodeId viewportFooter{};
        UI::UILayoutStyle viewportFooterStyle = fillWidth(28.0F);
        viewportFooterStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        viewportFooterStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
        viewportFooterStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        viewportFooterStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(8.0F, 3.0F);
        if (auto status = storeNode(createPanel(center, viewportFooterStyle, UI::UIStyleRoleId::PanelSurface),
                                    viewportFooter);
            !status) {
            return status;
        }
        UI::UINodeId cameraStatus{};
        if (auto status = storeNode(createLabel(viewportFooter, "Camera2D", fixedSize(70.0F, 20.0F), bodyText),
                                    cameraStatus);
            !status) {
            return status;
        }
        UI::UINodeId viewportModeStatus{};
        if (auto status = storeNode(createLabel(viewportFooter, "Select | Local | Snap",
                                                fixedSize(128.0F, 20.0F), secondaryText),
                                    viewportModeStatus);
            !status) {
            return status;
        }

        UI::UINodeId right{};
        UI::UILayoutStyle rightStyle = boundedDock(26.0F, 280.0F, 360.0F);
        rightStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        rightStyle.flexContainer.gap.row = 7.0F;
        rightStyle.padding = UI::UIEdgeSpacing::All(8.0F);
        if (auto status = storeNode(createPanel(body, rightStyle, UI::UIStyleRoleId::PanelSurface, dockClass_),
                                    right);
            !status) {
            return status;
        }

        UI::UINodeId inspectorHeader{};
        UI::UILayoutStyle inspectorHeaderStyle = fillWidth(30.0F);
        inspectorHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        inspectorHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        inspectorHeaderStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
        if (auto status = storeNode(createPanel(right, inspectorHeaderStyle, UI::UIStyleRoleId::None),
                                    inspectorHeader);
            !status) {
            return status;
        }
        UI::UINodeId inspectorTitle{};
        if (auto status = storeNode(createLabel(inspectorHeader, "Inspector", fixedSize(110.0F, 24.0F),
                                                sectionText),
                                    inspectorTitle);
            !status) {
            return status;
        }
        UI::UINodeId inspectorMode{};
        if (auto status = storeNode(createLabel(inspectorHeader, "Selection", fixedSize(58.0F, 20.0F),
                                                accentText),
                                    inspectorMode);
            !status) {
            return status;
        }

        UI::UILayoutStyle inspectorScrollStyle = growingRegion();
        inspectorScrollStyle.minMax.minHeight = UI::UILayoutLength::Px(180.0F);
        auto inspectorScroll = tree->createElement(right, UI::makeScrollViewElement(inspectorScrollStyle));
        if (!inspectorScroll) {
            return Tina::Core::failure(std::move(inspectorScroll.error()));
        }
        if (auto status = tree->setScrollViewStyle(
                *inspectorScroll,
                UI::UIScrollViewStyle{
                    .axes = UI::UIScrollAxes::Vertical,
                    .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                    .wheelStep = 36.0F,
                });
            !status) {
            return status;
        }
        counters_.inspectorScrollConfigured = true;

        UI::UINodeId inspectorContent{};
        UI::UILayoutStyle inspectorContentStyle = fillWidth(650.0F);
        inspectorContentStyle.padding = UI::UIEdgeSpacing::All(8.0F);
        inspectorContentStyle.flexContainer.gap.row = 7.0F;
        if (auto status = storeNode(createPanel(*inspectorScroll, inspectorContentStyle,
                                                UI::UIStyleRoleId::PanelSurface),
                                    inspectorContent);
            !status) {
            return status;
        }

        UI::UINodeId identityTitle{};
        if (auto status = storeNode(createLabel(inspectorContent, "Identity", fillWidth(22.0F), sectionText),
                                    identityTitle);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(inspectorContent, {}, fillWidth(22.0F), bodyText),
                                    inspectorName_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(inspectorContent, {}, fillWidth(22.0F), secondaryText),
                                    inspectorKind_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(inspectorContent, {}, fillWidth(42.0F), secondaryText),
                                    inspectorNote_);
            !status) {
            return status;
        }

        UI::UINodeId transformTitle{};
        if (auto status = storeNode(createLabel(inspectorContent, "Transform", fillWidth(22.0F), sectionText),
                                    transformTitle);
            !status) {
            return status;
        }
        const auto createTransformRow = [&](std::string_view caption, std::string_view value,
                                            bool enabled, UI::UINodeId& valueNode) -> Tina::Core::Status {
            UI::UINodeId row{};
            UI::UILayoutStyle rowStyle = fillWidth(30.0F);
            rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
            rowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
            rowStyle.flexContainer.gap.column = 8.0F;
            if (auto status = storeNode(createPanel(inspectorContent, rowStyle, UI::UIStyleRoleId::None), row);
                !status) {
                return status;
            }
            UI::UINodeId captionNode{};
            if (auto status = storeNode(createLabel(row, caption, fixedSize(74.0F, 20.0F), secondaryText),
                                        captionNode);
                !status) {
                return status;
            }
            UI::UILayoutStyle valueStyle = fixedSize(0.0F, 30.0F);
            valueStyle.size.width = UI::UILayoutLength::Auto();
            valueStyle.flexItem.grow = 1.0F;
            valueStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
            return storeNode(createTextEdit(row, value, valueStyle, enabled), valueNode);
        };
        if (auto status = createTransformRow("Position X", "0.000", true, inspectorPositionX_); !status) {
            return status;
        }
        if (auto status = createTransformRow("Position Y", "0.000", true, inspectorPositionY_); !status) {
            return status;
        }
        if (auto status = createTransformRow("Rotation Z", "0.000", true, inspectorRotationDegrees_); !status) {
            return status;
        }
        if (auto status = createTransformRow("Scale X", "1.000", true, inspectorScaleX_); !status) {
            return status;
        }
        if (auto status = createTransformRow("Scale Y", "1.000", true, inspectorScaleY_); !status) {
            return status;
        }
        if (auto status = storeNode(createButton(inspectorContent, "Apply Transform", fillWidth(34.0F)),
                                    applyTransformButton_);
            !status) {
            return status;
        }

        UI::UINodeId componentsTitle{};
        if (auto status = storeNode(createLabel(inspectorContent, "Components", fillWidth(22.0F), sectionText),
                                    componentsTitle);
            !status) {
            return status;
        }
        for (const std::string_view component : {std::string_view{"Transform"},
                                                 std::string_view{"SpriteRenderer2D"},
                                                 std::string_view{"Runtime preview"}}) {
            UI::UINodeId componentRow{};
            UI::UILayoutStyle componentRowStyle = fillWidth(24.0F);
            componentRowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
            componentRowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
            componentRowStyle.flexContainer.gap.column = 8.0F;
            if (auto status = storeNode(createPanel(inspectorContent, componentRowStyle,
                                                    UI::UIStyleRoleId::None),
                                        componentRow);
                !status) {
                return status;
            }
            UI::UINodeId componentCheckbox{};
            UI::UIElementDescriptor checkboxDesc = UI::makeCheckboxElement(fixedSize(20.0F, 20.0F));
            checkboxDesc.enabled = false;
            if (auto status = storeNode(tree->createElement(componentRow, checkboxDesc), componentCheckbox);
                !status) {
                return status;
            }
            if (auto status = tree->setChecked(componentCheckbox, true); !status) {
                return status;
            }
            UI::UINodeId componentLabel{};
            if (auto status = storeNode(createLabel(componentRow, component, fixedSize(170.0F, 20.0F), bodyText),
                                        componentLabel);
                !status) {
                return status;
            }
        }

        UI::UINodeId authoringTitle{};
        if (auto status = storeNode(createLabel(inspectorContent, "Authoring", fillWidth(22.0F), sectionText),
                                    authoringTitle);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(inspectorContent, "Move X +1", fillWidth(34.0F)), moveButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(inspectorContent, "One validated revision per command",
                                                fillWidth(20.0F), secondaryText),
                                    authoringHint_);
            !status) {
            return status;
        }

        UI::UINodeId documentTitle{};
        if (auto status = storeNode(createLabel(inspectorContent, "Document", fillWidth(22.0F), sectionText),
                                    documentTitle);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(inspectorContent, {}, fillWidth(22.0F), bodyText),
                                    inspectorDocument_);
            !status) {
            return status;
        }
        UI::UINodeId documentFormat{};
        if (auto status = storeNode(createLabel(inspectorContent, "World2D schema v1 | canonical",
                                                fillWidth(20.0F), secondaryText),
                                    documentFormat);
            !status) {
            return status;
        }

        UI::UINodeId statusBar{};
        UI::UILayoutStyle statusBarStyle = fillWidth(30.0F);
        statusBarStyle.flexItem.shrink = 0.0F;
        statusBarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        statusBarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        statusBarStyle.flexContainer.gap.column = 12.0F;
        statusBarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(10.0F, 4.0F);
        if (auto status = storeNode(createPanel(rootNode, statusBarStyle, UI::UIStyleRoleId::PanelSurface,
                                                dockClass_),
                                    statusBar);
            !status) {
            return status;
        }
        UI::UILayoutStyle statusSegmentStyle = fixedSize(0.0F, 20.0F);
        statusSegmentStyle.size.width = UI::UILayoutLength::Auto();
        statusSegmentStyle.flexItem.grow = 1.0F;
        statusSegmentStyle.flexItem.shrink = 1.0F;
        statusSegmentStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        if (auto status = storeNode(createLabel(statusBar, {}, statusSegmentStyle, compactText),
                                    statusDocument_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(statusBar, {}, statusSegmentStyle, secondaryText),
                                    statusPreview_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(statusBar, {}, fixedSize(190.0F, 20.0F), accentText),
                                    statusSelection_);
            !status) {
            return status;
        }

        if (auto status = tree->setButtonAction(
                moveButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::MoveSelectedPositiveX);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                applyTransformButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::ApplyTransform);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                undoButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::Undo);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                redoButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::Redo);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                saveButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::Save);
                }});
            !status) {
            return status;
        }
        counters_.authoringActionsWired = EditorActionCount;
        counters_.editorLayoutRegions = EditorLayoutRegionCount;
        counters_.viewportLayoutReady = true;
        counters_.documentPathConfigured = !options_.documentPathUtf8.empty();
        counters_.documentLoaded = documentLoaded_;
        counters_.savedSnapshotBytes = savedBaselineBytes_.size();

        auto initialSelection = tree->treeViewSelection(hierarchyTree_);
        if (!initialSelection) {
            return Tina::Core::failure(std::move(initialSelection.error()));
        }
        selectionKey_ = initialSelection->key;
        if (auto status = validateRuntimePreview(); !status) {
            return status;
        }
        if (auto status = refreshAuthoringUi(*tree); !status) {
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
        viewportNormalized_.reset();
        previewBindings_.clear();
        previewWorld_.reset();
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
        if (options_.autoDemo) {
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
            if (queuedFirstSelection_) {
                constexpr std::array commands{
                    EditorCommand::MoveSelectedPositiveX,
                    EditorCommand::ApplyTransform,
                    EditorCommand::Undo,
                    EditorCommand::Redo,
                    EditorCommand::Save,
                };
                const std::size_t commandCount =
                    options_.documentPathUtf8.empty() ? commands.size() - 1U : commands.size();
                if (autoAuthoringStage_ < commandCount) {
                    const EditorCommand command = commands[autoAuthoringStage_];
                    if (queueEditorCommand(command)) {
                        pendingAutoTransformInput_ = command == EditorCommand::ApplyTransform;
                        ++autoAuthoringStage_;
                    }
                }
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

    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        ++counters_.renderExtractions;
        counters_.gpuViewportSprites = 0;
        if (!viewportNormalized_.has_value()) {
            return Tina::Core::success();
        }
        if (!previewWorld_.has_value()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport has no canonical preview World");
        }

        const Tina::Scene::World2DEntityBinding* cameraBinding = findPreviewBinding(CameraKey);
        if (cameraBinding == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport is missing the Camera binding");
        }
        const Tina::Scene::WorldTransform* cameraTransform = previewWorld_->worldTransform(cameraBinding->entity);
        if (cameraTransform == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport is missing the Camera world transform");
        }

        auto& writer = context.renderSceneWriter();
        const Tina::Render::RenderCamera2DInput camera{
            .stableCameraKey = cameraBinding->stableEntityId,
            .centerX = cameraTransform->position.x,
            .centerY = cameraTransform->position.y,
            .rotationRadians = planarRotationDegrees(
                                   cameraTransform->rotation.x, cameraTransform->rotation.y,
                                   cameraTransform->rotation.z, cameraTransform->rotation.w) *
                               DegreesToRadians,
            .worldWidth = PreviewWorldWidth,
            .worldHeight = PreviewWorldHeight,
            .actualPixelsPerMeter = (std::max)(1.0F, viewportLogicalRect_.height / PreviewWorldHeight),
            .normalizedViewport = *viewportNormalized_,
            .pixelSnap = Tina::Render::RenderPixelSnapPolicy::Disabled,
        };
        if (auto status = writer.setCamera2D(camera); !status) {
            return status;
        }

        auto texture = spriteFrameResource_.intern(context.frameResourceSink(), 1);
        if (!texture) {
            return Tina::Core::failure(std::move(texture.error()));
        }
        struct ProxySpec final {
            UI::UITreeViewItemKey hierarchyKey = UI::InvalidUITreeViewItemKey;
            float widthMeters = 1.0F;
            float heightMeters = 1.0F;
            Tina::Core::i32 orderInLayer = 0;
            u8 red = 255;
            u8 green = 255;
            u8 blue = 255;
        };
        constexpr std::array<ProxySpec, GpuViewportSpriteCount> ProxySpecs{{
            {.hierarchyKey = TileMapKey, .widthMeters = 5.0F, .heightMeters = 3.0F,
             .orderInLayer = -20, .red = 178, .green = 156, .blue = 235},
            {.hierarchyKey = CameraKey, .widthMeters = 0.6F, .heightMeters = 0.6F,
             .orderInLayer = 10, .red = 114, .green = 167, .blue = 216},
            {.hierarchyKey = PlayerKey, .widthMeters = 1.0F, .heightMeters = 1.4F,
             .orderInLayer = 20, .red = 231, .green = 182, .blue = 90},
            {.hierarchyKey = LightsKey, .widthMeters = 0.8F, .heightMeters = 0.8F,
             .orderInLayer = 30, .red = 125, .green = 211, .blue = 167},
        }};

        for (const ProxySpec& spec : ProxySpecs) {
            const Tina::Scene::World2DEntityBinding* binding = findPreviewBinding(spec.hierarchyKey);
            if (binding == nullptr) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "editor GPU viewport is missing a canonical proxy binding");
            }
            const Tina::Scene::WorldTransform* transform = previewWorld_->worldTransform(binding->entity);
            if (transform == nullptr) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "editor GPU viewport is missing a canonical proxy transform");
            }
            const bool selected = stableEntityIdForHierarchyItem(selectionKey_) == binding->stableEntityId;
            const Tina::Render::RenderSprite2DInput sprite{
                .texture = *texture,
                .stableEntityKey = binding->stableEntityId,
                .centerX = transform->position.x,
                .centerY = transform->position.y,
                .rotationRadians = planarRotationDegrees(
                                       transform->rotation.x, transform->rotation.y,
                                       transform->rotation.z, transform->rotation.w) *
                                   DegreesToRadians,
                .widthMeters = spec.widthMeters,
                .heightMeters = spec.heightMeters,
                .scaleX = visibleProxyScale(transform->scale.x),
                .scaleY = visibleProxyScale(transform->scale.y),
                .sortingLayer = 0,
                .orderInLayer = spec.orderInLayer,
                .red = selected ? u8{242} : spec.red,
                .green = selected ? u8{245} : spec.green,
                .blue = selected ? u8{247} : spec.blue,
                .alpha = 220,
            };
            if (auto status = writer.addSprite2D(sprite); !status) {
                return status;
            }
            ++counters_.gpuViewportSprites;
        }
        counters_.gpuViewportDocumentRevision = previewRevision_;
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
        if (auto status = updateGpuViewport(*tree); !status) {
            return status;
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
            if (auto status = refreshAuthoringUi(*tree); !status) {
                return status;
            }
        }
        if (pendingAutoTransformInput_ && pendingEditorCommand_ == EditorCommand::ApplyTransform) {
            pendingAutoTransformInput_ = false;
            if (auto status = tree->setText(inspectorPositionX_, "2.5"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorPositionY_, "-1.25"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorRotationDegrees_, "30.0"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorScaleX_, "1.25"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorScaleY_, "0.75"); !status) {
                return status;
            }
        }
        if (pendingEditorCommand_.has_value()) {
            if (auto status = executeEditorCommand(*tree); !status) {
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
    bool queueEditorCommand(EditorCommand command) noexcept
    {
        if (pendingEditorCommand_.has_value()) {
            return false;
        }
        pendingEditorCommand_ = command;
        return true;
    }

    [[nodiscard]] const Tina::Scene::World2DEntityBinding*
    findPreviewBinding(UI::UITreeViewItemKey hierarchyKey) const noexcept
    {
        const u32 stableEntityId = stableEntityIdForHierarchyItem(hierarchyKey);
        const auto binding = std::find_if(
            previewBindings_.begin(), previewBindings_.end(),
            [stableEntityId](const Tina::Scene::World2DEntityBinding& candidate) {
                return candidate.stableEntityId == stableEntityId;
            });
        return binding == previewBindings_.end() ? nullptr : &*binding;
    }

    [[nodiscard]] static float visibleProxyScale(float scale) noexcept
    {
        constexpr float MinimumVisibleScale = 0.05F;
        const float magnitude = (std::max)(std::abs(scale), MinimumVisibleScale);
        return std::signbit(scale) ? -magnitude : magnitude;
    }

    [[nodiscard]] Tina::Core::Status updateGpuViewport(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        auto viewportRect = tree.committedLayoutRect(viewportPreviewLayer_);
        if (!viewportRect) {
            return Tina::Core::failure(std::move(viewportRect.error()));
        }
        auto rootRect = tree.committedLayoutRect(uiRoot_.rootNodeId());
        if (!rootRect) {
            return Tina::Core::failure(std::move(rootRect.error()));
        }
        if (!std::isfinite(rootRect->x) || !std::isfinite(rootRect->y) ||
            !std::isfinite(rootRect->width) || !std::isfinite(rootRect->height) ||
            !std::isfinite(viewportRect->x) || !std::isfinite(viewportRect->y) ||
            !std::isfinite(viewportRect->width) || !std::isfinite(viewportRect->height) ||
            rootRect->width <= 0.0F || rootRect->height <= 0.0F ||
            viewportRect->width <= 0.0F || viewportRect->height <= 0.0F) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport committed layout is not usable");
        }

        const double rootWidth = rootRect->width;
        const double rootHeight = rootRect->height;
        const double left = std::clamp(
            (static_cast<double>(viewportRect->x) - rootRect->x) / rootWidth, 0.0, 1.0);
        const double top = std::clamp(
            (static_cast<double>(viewportRect->y) - rootRect->y) / rootHeight, 0.0, 1.0);
        const double right = std::clamp(
            (static_cast<double>(viewportRect->x) + viewportRect->width - rootRect->x) /
                rootWidth,
            0.0, 1.0);
        const double bottom = std::clamp(
            (static_cast<double>(viewportRect->y) + viewportRect->height - rootRect->y) /
                rootHeight,
            0.0, 1.0);
        if (!(right > left) || !(bottom > top)) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport lies outside the committed UI root");
        }

        Tina::Render::RenderNormalizedViewport normalized{
            .x = static_cast<float>(left),
            .y = static_cast<float>(top),
            .width = static_cast<float>(right - left),
            .height = static_cast<float>(bottom - top),
        };
        if (static_cast<double>(normalized.x) + normalized.width > 1.0) {
            normalized.width = std::nextafter(normalized.width, 0.0F);
        }
        if (static_cast<double>(normalized.y) + normalized.height > 1.0) {
            normalized.height = std::nextafter(normalized.height, 0.0F);
        }
        if (normalized.width <= 0.0F || normalized.height <= 0.0F) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport normalized extent is empty");
        }

        viewportLogicalRect_ = *viewportRect;
        viewportNormalized_ = normalized;
        counters_.viewportLogicalX = viewportRect->x;
        counters_.viewportLogicalY = viewportRect->y;
        counters_.viewportLogicalWidth = viewportRect->width;
        counters_.viewportLogicalHeight = viewportRect->height;
        counters_.viewportNormalizedX = normalized.x;
        counters_.viewportNormalizedY = normalized.y;
        counters_.viewportNormalizedWidth = normalized.width;
        counters_.viewportNormalizedHeight = normalized.height;
        counters_.gpuViewportReady = true;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status executeEditorCommand(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const EditorCommand command = *pendingEditorCommand_;
        pendingEditorCommand_.reset();

        Tina::Core::Status status = Tina::Core::success();
        bool requiresPreviewValidation = false;
        switch (command) {
        case EditorCommand::MoveSelectedPositiveX:
            status = moveSelectedPositiveX();
            if (status) {
                ++counters_.authoringEdits;
                requiresPreviewValidation = true;
                authoringFeedback_ = "Move X +1 applied as one document revision";
            }
            break;
        case EditorCommand::ApplyTransform: {
            auto positionXText = tree.text(inspectorPositionX_);
            if (!positionXText) {
                return Tina::Core::failure(std::move(positionXText.error()));
            }
            auto positionYText = tree.text(inspectorPositionY_);
            if (!positionYText) {
                return Tina::Core::failure(std::move(positionYText.error()));
            }
            auto rotationDegreesText = tree.text(inspectorRotationDegrees_);
            if (!rotationDegreesText) {
                return Tina::Core::failure(std::move(rotationDegreesText.error()));
            }
            auto scaleXText = tree.text(inspectorScaleX_);
            if (!scaleXText) {
                return Tina::Core::failure(std::move(scaleXText.error()));
            }
            auto scaleYText = tree.text(inspectorScaleY_);
            if (!scaleYText) {
                return Tina::Core::failure(std::move(scaleYText.error()));
            }
            float positionX = 0.0F;
            float positionY = 0.0F;
            float rotationDegrees = 0.0F;
            float scaleX = 1.0F;
            float scaleY = 1.0F;
            if (!parseFiniteFloat(*positionXText, positionX) || !parseFiniteFloat(*positionYText, positionY) ||
                !parseFiniteFloat(*rotationDegreesText, rotationDegrees) ||
                !parseFiniteFloat(*scaleXText, scaleX) || !parseFiniteFloat(*scaleYText, scaleY)) {
                ++counters_.inspectorRejectedTransactions;
                authoringFeedback_ = "Transform rejected: enter finite decimal values";
                return refreshAuthoringUi(tree);
            }
            status = applySelectedTransform(positionX, positionY, rotationDegrees, scaleX, scaleY);
            if (status) {
                ++counters_.authoringEdits;
                ++counters_.inspectorTransactions;
                requiresPreviewValidation = true;
                authoringFeedback_ = "Transform applied as one document revision";
            }
            break;
        }
        case EditorCommand::Undo:
            status = document_.undo();
            if (status) {
                ++counters_.authoringUndos;
                requiresPreviewValidation = true;
                authoringFeedback_ = "Undo restored the previous canonical snapshot";
            }
            break;
        case EditorCommand::Redo:
            status = document_.redo();
            if (status) {
                ++counters_.authoringRedos;
                requiresPreviewValidation = true;
                authoringFeedback_ = "Redo restored the next canonical snapshot";
            }
            break;
        case EditorCommand::Save:
            status = saveCurrentDocument();
            if (status) {
                authoringFeedback_ = "Canonical World2D document saved atomically";
            }
            break;
        }
        if (!status) {
            return status;
        }
        if (requiresPreviewValidation) {
            if (auto previewStatus = validateRuntimePreview(); !previewStatus) {
                return previewStatus;
            }
        }
        return refreshAuthoringUi(tree);
    }

    [[nodiscard]] Tina::Core::Status saveCurrentDocument()
    {
        if (options_.documentPathUtf8.empty()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                       "editor shell has no configured document path");
        }
        try {
            std::vector<std::byte> savedCandidate(document_.snapshotBytes().begin(),
                                                  document_.snapshotBytes().end());
            if (auto status = Tina::Editor::saveWorld2DAuthoringDocument(
                    options_.documentPathUtf8, document_);
                !status) {
                return status;
            }
            savedBaselineBytes_ = std::move(savedCandidate);
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "editor shell could not retain the saved document baseline");
        }
        ++counters_.authoringSaves;
        counters_.savedSnapshotBytes = savedBaselineBytes_.size();
        return Tina::Core::success();
    }

    [[nodiscard]] bool isDocumentDirty() const noexcept
    {
        const std::span<const std::byte> current = document_.snapshotBytes();
        return savedBaselineBytes_.size() != current.size() ||
               !std::equal(savedBaselineBytes_.begin(), savedBaselineBytes_.end(), current.begin());
    }

    [[nodiscard]] Tina::Core::Status moveSelectedPositiveX()
    {
        const u32 stableEntityId = stableEntityIdForHierarchyItem(selectionKey_);
        if (stableEntityId == 0U) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                       "editor selection has no authoring entity");
        }

        std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
        auto snapshot = document_.parseCurrentSnapshot(storage);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        const auto entity = std::find_if(storage.begin(), storage.end(), [stableEntityId](const auto& candidate) {
            return candidate.stableEntityId == stableEntityId;
        });
        if (entity == storage.end()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                       "editor selection is absent from the authoring document");
        }
        auto edited = *entity;
        edited.positionX += 1.0F;
        return document_.upsertEntity(edited);
    }

    [[nodiscard]] Tina::Core::Status applySelectedTransform(float positionX, float positionY,
                                                            float rotationDegrees, float scaleX,
                                                            float scaleY)
    {
        const u32 stableEntityId = stableEntityIdForHierarchyItem(selectionKey_);
        if (stableEntityId == 0U) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                       "editor selection has no authoring entity");
        }

        std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
        auto snapshot = document_.parseCurrentSnapshot(storage);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        const auto entity = std::find_if(storage.begin(), storage.end(), [stableEntityId](const auto& candidate) {
            return candidate.stableEntityId == stableEntityId;
        });
        if (entity == storage.end()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                       "editor selection is absent from the authoring document");
        }
        auto edited = *entity;
        edited.positionX = positionX;
        edited.positionY = positionY;
        const float normalizedDegrees = std::remainder(rotationDegrees, 360.0F);
        const float halfRadians = normalizedDegrees * DegreesToRadians * 0.5F;
        edited.rotationX = 0.0F;
        edited.rotationY = 0.0F;
        edited.rotationZ = std::sin(halfRadians);
        edited.rotationW = std::cos(halfRadians);
        edited.scaleX = scaleX;
        edited.scaleY = scaleY;
        return document_.upsertEntity(edited);
    }

    [[nodiscard]] Tina::Core::Status validateRuntimePreview()
    {
        counters_.runtimePreviewValid = false;
        std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
        auto snapshot = document_.parseCurrentSnapshot(storage);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        auto world = Tina::Scene::World::Create({.entityCapacity = AuthoringEntityCapacity});
        if (!world) {
            return Tina::Core::failure(std::move(world.error()));
        }
        auto bindings = Tina::Scene::instantiateWorld2DSnapshot(*world, *snapshot);
        if (!bindings) {
            return Tina::Core::failure(std::move(bindings.error()));
        }
        if (bindings->size() != document_.entityCount() || world->entityCount() != document_.entityCount()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor runtime preview entity count mismatch");
        }
        constexpr std::array proxyHierarchyKeys{
            CameraKey,
            PlayerKey,
            LightsKey,
            TileMapKey,
        };
        for (const UI::UITreeViewItemKey hierarchyKey : proxyHierarchyKeys) {
            const u32 stableEntityId = stableEntityIdForHierarchyItem(hierarchyKey);
            const auto binding = std::find_if(
                bindings->begin(), bindings->end(),
                [stableEntityId](const Tina::Scene::World2DEntityBinding& candidate) {
                    return candidate.stableEntityId == stableEntityId;
                });
            if (binding == bindings->end() || world->worldTransform(binding->entity) == nullptr) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "editor runtime preview is missing a GPU proxy transform");
            }
        }
        const auto player = std::find_if(bindings->begin(), bindings->end(), [](const auto& binding) {
            return binding.stableEntityId == 3U;
        });
        if (player == bindings->end()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor runtime preview is missing the Player entity");
        }
        const Tina::Scene::LocalTransform* playerTransform = world->localTransform(player->entity);
        if (playerTransform == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor runtime preview is missing the Player transform");
        }

        ++counters_.runtimePreviewInstantiations;
        counters_.documentRevision = document_.revision();
        counters_.documentEntityCount = document_.entityCount();
        counters_.documentUndoDepth = document_.undoDepth();
        counters_.documentRedoDepth = document_.redoDepth();
        counters_.cookPreviewBytes = document_.snapshotBytes().size();
        counters_.finalPlayerPositionX = playerTransform->position.x;
        counters_.finalPlayerPositionY = playerTransform->position.y;
        counters_.finalPlayerRotationDegrees = planarRotationDegrees(
            playerTransform->rotation.x, playerTransform->rotation.y, playerTransform->rotation.z,
            playerTransform->rotation.w);
        counters_.finalPlayerScaleX = playerTransform->scale.x;
        counters_.finalPlayerScaleY = playerTransform->scale.y;
        previewWorld_.emplace(std::move(*world));
        previewBindings_ = std::move(*bindings);
        previewRevision_ = document_.revision();
        counters_.runtimePreviewValid = true;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status refreshAuthoringUi(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const bool dirty = isDocumentDirty();
        const bool pathConfigured = !options_.documentPathUtf8.empty();
        const bool selectionEditable = stableEntityIdForHierarchyItem(selectionKey_) != 0U;
        counters_.documentDirty = dirty;
        counters_.documentSaved = pathConfigured && !dirty;

        if (auto status = publishInspector(tree, selectionKey_); !status) {
            return status;
        }
        std::string documentStatus = "Revision ";
        documentStatus += std::to_string(document_.revision());
        documentStatus += " | Undo ";
        documentStatus += std::to_string(document_.undoDepth());
        documentStatus += " | Redo ";
        documentStatus += std::to_string(document_.redoDepth());
        documentStatus += pathConfigured ? (dirty ? " | Modified" : " | Saved") : " | Unsaved";
        if (auto status = tree.setText(inspectorDocument_, documentStatus); !status) {
            return status;
        }
        std::string statusDocument = "World2D v1  |  ";
        statusDocument += std::to_string(document_.entityCount());
        statusDocument += " entities  |  Revision ";
        statusDocument += std::to_string(document_.revision());
        statusDocument += pathConfigured ? (dirty ? "  |  Modified" : "  |  Saved") : "  |  Unsaved";
        if (auto status = tree.setText(statusDocument_, statusDocument); !status) {
            return status;
        }
        const std::string_view pathStatus = pathConfigured
                                                ? (dirty ? "Save target configured | Modified"
                                                         : "Save target configured | Saved")
                                                : "No save path | Unsaved";
        if (auto status = tree.setText(toolbarPath_, pathStatus); !status) {
            return status;
        }
        std::string statusPreview = "Runtime preview: ";
        statusPreview += counters_.runtimePreviewValid ? "valid" : "invalid";
        statusPreview += "  |  Cook ";
        statusPreview += std::to_string(document_.snapshotBytes().size());
        statusPreview += " B";
        if (auto status = tree.setText(statusPreview_, statusPreview); !status) {
            return status;
        }
        std::string selectionSummary = "Selected: ";
        selectionSummary += hierarchyLabel(selectionKey_);
        const u32 selectedEntityId = stableEntityIdForHierarchyItem(selectionKey_);
        if (selectedEntityId != 0U) {
            selectionSummary += "  |  ID ";
            selectionSummary += std::to_string(selectedEntityId);
        }
        if (auto status = tree.setText(hierarchySelectionSummary_, selectionSummary); !status) {
            return status;
        }
        std::string statusSelection = "Selected: ";
        statusSelection += hierarchyLabel(selectionKey_);
        if (auto status = tree.setText(statusSelection_, statusSelection); !status) {
            return status;
        }
        if (auto status = tree.setText(authoringHint_, authoringFeedback_); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorPositionX_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorPositionY_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorRotationDegrees_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorScaleX_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorScaleY_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(applyTransformButton_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(moveButton_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(undoButton_, document_.canUndo()); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(redoButton_, document_.canRedo()); !status) {
            return status;
        }
        return tree.setEnabled(saveButton_, pathConfigured && dirty);
    }

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
        note += hierarchyAuthoringNote(key);
        float positionX = 0.0F;
        float positionY = 0.0F;
        float rotationDegrees = 0.0F;
        float scaleX = 1.0F;
        float scaleY = 1.0F;
        bool hasEntity = false;
        const u32 stableEntityId = stableEntityIdForHierarchyItem(key);
        if (stableEntityId != 0U) {
            std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
            auto snapshot = document_.parseCurrentSnapshot(storage);
            if (!snapshot) {
                return Tina::Core::failure(std::move(snapshot.error()));
            }
            const auto entity = std::find_if(storage.begin(), storage.end(), [stableEntityId](const auto& candidate) {
                return candidate.stableEntityId == stableEntityId;
            });
            if (entity != storage.end()) {
                hasEntity = true;
                positionX = entity->positionX;
                positionY = entity->positionY;
                rotationDegrees = planarRotationDegrees(entity->rotationX, entity->rotationY,
                                                        entity->rotationZ, entity->rotationW);
                scaleX = entity->scaleX;
                scaleY = entity->scaleY;
                note += " X=";
                note += std::to_string(entity->positionX);
            }
        }
        if (auto status = tree.setText(inspectorNote_, note); !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorPositionX_, hasEntity ? std::to_string(positionX) : "n/a");
            !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorPositionY_, hasEntity ? std::to_string(positionY) : "n/a");
            !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorRotationDegrees_,
                                       hasEntity ? std::to_string(rotationDegrees) : "n/a");
            !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorScaleX_, hasEntity ? std::to_string(scaleX) : "n/a");
            !status) {
            return status;
        }
        return tree.setText(inspectorScaleY_, hasEntity ? std::to_string(scaleY) : "n/a");
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
    Tina::Editor::World2DAuthoringDocument document_;
    bool documentLoaded_ = false;
    Tina::UI::UIRootOwner uiRoot_{};
    UI::UINodeId hierarchyTree_{};
    UI::UINodeId inspectorName_{};
    UI::UINodeId inspectorKind_{};
    UI::UINodeId inspectorNote_{};
    UI::UINodeId inspectorDocument_{};
    UI::UINodeId hierarchySelectionSummary_{};
    UI::UINodeId viewportPreviewLayer_{};
    UI::UINodeId inspectorPositionX_{};
    UI::UINodeId inspectorPositionY_{};
    UI::UINodeId inspectorRotationDegrees_{};
    UI::UINodeId inspectorScaleX_{};
    UI::UINodeId inspectorScaleY_{};
    UI::UINodeId authoringHint_{};
    UI::UINodeId statusDocument_{};
    UI::UINodeId statusPreview_{};
    UI::UINodeId statusSelection_{};
    UI::UINodeId toolbarPath_{};
    UI::UINodeId moveButton_{};
    UI::UINodeId applyTransformButton_{};
    UI::UINodeId undoButton_{};
    UI::UINodeId redoButton_{};
    UI::UINodeId saveButton_{};
    UI::UIStyleClassId dockClass_{};
    UI::UIStyleClassId viewportClass_{};
    UI::UIStyleTokenId viewportToken_{};
    UI::UITreeViewItemKey selectionKey_ = UI::InvalidUITreeViewItemKey;
    bool sceneExpanded_ = true;
    bool playerExpanded_ = true;
    bool queuedFirstSelection_ = false;
    bool queuedSecondSelection_ = false;
    bool pendingAutoTransformInput_ = false;
    u32 autoAuthoringStage_ = 0;
    std::optional<Tina::Scene::World> previewWorld_{};
    std::vector<Tina::Scene::World2DEntityBinding> previewBindings_{};
    u64 previewRevision_ = 0;
    UI::UILogicalRect viewportLogicalRect_{};
    std::optional<Tina::Render::RenderNormalizedViewport> viewportNormalized_{};
    mutable Tina::Samples::SampleSpriteFrameResource spriteFrameResource_{};
    std::vector<std::byte> savedBaselineBytes_{};
    std::string authoringFeedback_ = "One validated revision per command";
    std::optional<u64> pendingSelectionIndex_{};
    std::optional<EditorCommand> pendingEditorCommand_{};
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
        auto initialDocument = createAuthoringDocument(options_.documentPathUtf8);
        if (!initialDocument) {
            return Tina::Core::failure(std::move(initialDocument.error()));
        }
        try {
            std::vector<std::byte> savedBaselineBytes;
            if (initialDocument->loadedFromPath) {
                savedBaselineBytes.assign(initialDocument->document.snapshotBytes().begin(),
                                          initialDocument->document.snapshotBytes().end());
            }
            std::unique_ptr<Tina::IGameState> state = std::make_unique<EditorShellState>(
                options_, counters_, std::move(initialDocument->document), std::move(savedBaselineBytes),
                initialDocument->loadedFromPath);
            return state;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "editor shell could not retain the opened document baseline");
        }
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
    config.primaryWindow.title = "Tina 2D Editor";
    config.primaryWindow.initialLogicalExtent = {WindowLogicalWidth, WindowLogicalHeight};
    config.primaryWindow.initiallyVisible = true;
    config.renderSceneCapacities.spriteCapacity = 8;
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
    const bool documentPathConfigured = !options.documentPathUtf8.empty();
    if (exitReason != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "editor shell stopped for an unexpected reason");
    }
    if (counters.frameUpdates != options.targetFrameCount || counters.stateEnters != 1 ||
        counters.stateExits != 1 || counters.applicationShutdowns != 1 || counters.uiRootsCreated != 1 ||
        counters.uiRootsReleased != 1 || !counters.selectionVerified || counters.hierarchyLogicalItems == 0 ||
        !counters.stylesheetInstalled || counters.styleRegisteredClasses != 2 ||
        counters.styleRegisteredTokens != 2 || counters.styleActiveRules != 2 ||
        counters.authoringActionsWired != EditorActionCount || !counters.runtimePreviewValid ||
        counters.editorLayoutRegions != EditorLayoutRegionCount || !counters.viewportLayoutReady ||
        !counters.gpuViewportReady || !counters.inspectorScrollConfigured ||
        counters.documentPathConfigured != documentPathConfigured ||
        counters.runtimePreviewInstantiations < 1 || counters.renderExtractions != options.targetFrameCount ||
        (options.targetFrameCount > 1 && counters.gpuViewportSprites != GpuViewportSpriteCount) ||
        counters.viewportLogicalWidth <= 0.0F || counters.viewportLogicalHeight <= 0.0F ||
        counters.viewportNormalizedX < 0.0F || counters.viewportNormalizedY < 0.0F ||
        counters.viewportNormalizedWidth <= 0.0F || counters.viewportNormalizedHeight <= 0.0F ||
        static_cast<double>(counters.viewportNormalizedX) + counters.viewportNormalizedWidth > 1.0 ||
        static_cast<double>(counters.viewportNormalizedY) + counters.viewportNormalizedHeight > 1.0 ||
        counters.documentEntityCount != InitialAuthoringEntityCount ||
        counters.cookPreviewBytes != Tina::AssetFormat::World2DSnapshotWire::HeaderBytes +
                                         InitialAuthoringEntityCount *
                                             Tina::AssetFormat::World2DSnapshotWire::EntityBytes) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "editor shell lifecycle counters did not match contract");
    }
    if (options.autoDemo) {
        if (counters.hierarchySelectionChanges < 1 || counters.finalSelectionKey != TileMapKey ||
            counters.styleTokenUpdates < 2 || counters.authoringEdits != 2 ||
            counters.inspectorTransactions != 1 || counters.inspectorRejectedTransactions != 0 ||
            counters.authoringUndos != 1 || counters.authoringRedos != 1 ||
            counters.runtimePreviewInstantiations != 5 || counters.documentRevision != 6 ||
            counters.gpuViewportDocumentRevision != counters.documentRevision ||
            counters.documentUndoDepth != 2 || counters.documentRedoDepth != 0 ||
            counters.finalPlayerPositionX != 2.5F || counters.finalPlayerPositionY != -1.25F ||
            std::abs(counters.finalPlayerRotationDegrees - 30.0F) > 0.001F ||
            counters.finalPlayerScaleX != 1.25F || counters.finalPlayerScaleY != 0.75F) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor shell automatic authoring demo did not finish");
        }
        if (documentPathConfigured) {
            if (counters.authoringSaves != 1 || !counters.documentSaved || counters.documentDirty ||
                counters.savedSnapshotBytes != counters.cookPreviewBytes) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "editor shell automatic save did not finish");
            }
        } else if (counters.authoringSaves != 0 || counters.documentSaved || !counters.documentDirty ||
                   counters.savedSnapshotBytes != 0) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor shell reported an unexpected saved document");
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

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_editor_shell\",\"readOnly\":false"
              << ",\"editorModule\":true,\"stylesheetInstalled\":"
              << (counters.stylesheetInstalled ? "true" : "false") << ",\"frames\":" << counters.frameUpdates
              << ",\"targetFrames\":" << options.targetFrameCount
              << ",\"frameDelayMs\":" << options.frameDelayMilliseconds
              << ",\"autoDemo\":" << (options.autoDemo ? "true" : "false") << ",\"exit\":";
    writeJsonString(std::cout, runExitReasonName(*runResult));
    std::cout << ",\"documentPathConfigured\":"
              << (counters.documentPathConfigured ? "true" : "false") << ",\"documentPath\":";
    writeJsonString(std::cout, options.documentPathUtf8);
    std::cout << ",\"documentLoaded\":" << (counters.documentLoaded ? "true" : "false")
              << ",\"stateEnters\":" << counters.stateEnters << ",\"stateExits\":" << counters.stateExits
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
              << ",\"authoringActionsWired\":" << counters.authoringActionsWired
              << ",\"authoringEdits\":" << counters.authoringEdits
              << ",\"authoringUndos\":" << counters.authoringUndos
              << ",\"authoringRedos\":" << counters.authoringRedos
              << ",\"authoringSaves\":" << counters.authoringSaves
              << ",\"inspectorTransactions\":" << counters.inspectorTransactions
              << ",\"inspectorRejectedTransactions\":" << counters.inspectorRejectedTransactions
              << ",\"savedSnapshotBytes\":" << counters.savedSnapshotBytes
              << ",\"editorLayoutRegions\":" << counters.editorLayoutRegions
              << ",\"viewportLayoutReady\":" << (counters.viewportLayoutReady ? "true" : "false")
              << ",\"inspectorScrollConfigured\":"
              << (counters.inspectorScrollConfigured ? "true" : "false")
              << ",\"renderExtractions\":" << counters.renderExtractions
              << ",\"gpuViewportSprites\":" << counters.gpuViewportSprites
              << ",\"gpuViewportDocumentRevision\":" << counters.gpuViewportDocumentRevision
              << ",\"gpuViewportReady\":" << (counters.gpuViewportReady ? "true" : "false")
              << ",\"viewportLogicalX\":" << counters.viewportLogicalX
              << ",\"viewportLogicalY\":" << counters.viewportLogicalY
              << ",\"viewportLogicalWidth\":" << counters.viewportLogicalWidth
              << ",\"viewportLogicalHeight\":" << counters.viewportLogicalHeight
              << ",\"viewportNormalizedX\":" << counters.viewportNormalizedX
              << ",\"viewportNormalizedY\":" << counters.viewportNormalizedY
              << ",\"viewportNormalizedWidth\":" << counters.viewportNormalizedWidth
              << ",\"viewportNormalizedHeight\":" << counters.viewportNormalizedHeight
              << ",\"runtimePreviewInstantiations\":" << counters.runtimePreviewInstantiations
              << ",\"runtimePreviewValid\":" << (counters.runtimePreviewValid ? "true" : "false")
              << ",\"documentRevision\":" << counters.documentRevision
              << ",\"documentEntityCount\":" << counters.documentEntityCount
              << ",\"documentUndoDepth\":" << counters.documentUndoDepth
              << ",\"documentRedoDepth\":" << counters.documentRedoDepth
              << ",\"documentSaved\":" << (counters.documentSaved ? "true" : "false")
              << ",\"documentDirty\":" << (counters.documentDirty ? "true" : "false")
              << ",\"cookPreviewBytes\":" << counters.cookPreviewBytes
              << ",\"finalPlayerPositionX\":" << counters.finalPlayerPositionX
              << ",\"finalPlayerPositionY\":" << counters.finalPlayerPositionY
              << ",\"finalPlayerRotationDegrees\":" << counters.finalPlayerRotationDegrees
              << ",\"finalPlayerScaleX\":" << counters.finalPlayerScaleX
              << ",\"finalPlayerScaleY\":" << counters.finalPlayerScaleY
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
