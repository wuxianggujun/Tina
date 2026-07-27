#include "Product3DUI.hpp"

#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/ui/UITheme.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace Tina::Sample3D {
namespace {

inline constexpr Core::u64 PanelCount = 5;
inline constexpr Core::u64 LabelCount = 9;

struct Rect final {
    float left = 0.0F;
    float top = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

[[nodiscard]] UI::UILayoutStyle absoluteStyle(Rect rect, UI::UIEdgeSpacing padding = {}) noexcept
{
    UI::UILayoutStyle style{};
    style.position = UI::UILayoutPositionMode::AbsoluteOverlay;
    style.absoluteInset.left = UI::UILayoutLength::Px(rect.left);
    style.absoluteInset.top = UI::UILayoutLength::Px(rect.top);
    style.size.width = UI::UILayoutLength::Px(rect.width);
    style.size.height = UI::UILayoutLength::Px(rect.height);
    style.padding = padding;
    return style;
}

[[nodiscard]] UI::UILayoutStyle rootStyle() noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Percent(100.0F);
    style.size.height = UI::UILayoutLength::Percent(100.0F);
    return style;
}

[[nodiscard]] Core::Status storeNode(Core::Result<UI::UINodeId>&& result, UI::UINodeId& destination)
{
    if (!result)
    {
        return Core::failure(std::move(result.error()));
    }
    destination = *result;
    return Core::success();
}

[[nodiscard]] Core::Result<UI::UINodeId> createPanel(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent, Rect rect)
{
    auto node = tree.createPanel(parent);
    if (!node)
    {
        return Core::failure(std::move(node.error()));
    }
    if (Core::Status status = tree.setLayoutStyle(*node, absoluteStyle(rect)); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return *node;
}

[[nodiscard]] Core::Result<UI::UINodeId> createLabel(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent, Rect rect,
                                                     std::string_view text)
{
    auto node = tree.createLabel(parent);
    if (!node)
    {
        return Core::failure(std::move(node.error()));
    }
    if (Core::Status status = tree.setLayoutStyle(*node, absoluteStyle(rect)); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (Core::Status status = tree.setText(*node, text); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return *node;
}

[[nodiscard]] Core::Result<UI::UINodeId> createButton(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent, Rect rect,
                                                      std::string_view text)
{
    auto node = tree.createButton(parent);
    if (!node)
    {
        return Core::failure(std::move(node.error()));
    }
    if (Core::Status status =
            tree.setLayoutStyle(*node, absoluteStyle(rect, UI::UIEdgeSpacing::HorizontalVertical(14.0F, 8.0F)));
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (Core::Status status = tree.setText(*node, text); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return *node;
}

[[nodiscard]] Product3DUITheme oppositeTheme(Product3DUITheme theme) noexcept
{
    return theme == Product3DUITheme::Dark ? Product3DUITheme::Light : Product3DUITheme::Dark;
}

[[nodiscard]] const UI::UITheme& themeFor(Product3DUITheme mode) noexcept
{
    static constexpr UI::UITheme Dark = UI::makeDefaultProductTheme();
    static constexpr UI::UITheme Light = UI::makeLightProductTheme();
    return mode == Product3DUITheme::Dark ? Dark : Light;
}

} // namespace

Product3DUI::Product3DUI(Product3DUIEvidence& evidence) noexcept : evidence_(&evidence)
{
}

Product3DUI::~Product3DUI()
{
    release();
}

Core::Status Product3DUI::build(GameStateEnterContext& context, Product3DUIConfig config)
{
    if (root_)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "3D product UI root is already built");
    }
    if (config.targetFrameCount == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "3D product UI requires a non-zero frame count");
    }

    config_ = config;
    currentTheme_ = config.initialTheme;
    evidence_->themeDemoRequested = config.automatedThemeDemo;
    evidence_->initialThemeLight = config.initialTheme == Product3DUITheme::Light;
    evidence_->finalThemeLight = evidence_->initialThemeLight;

    auto rootBuilder = context.primaryWindowUIRootBuilder();
    if (!rootBuilder)
    {
        return Core::failure(std::move(rootBuilder.error()));
    }
    auto root = rootBuilder->createRoot();
    if (!root)
    {
        return Core::failure(std::move(root.error()));
    }
    auto tree = rootBuilder->treeUpdater(*root);
    if (!tree)
    {
        return Core::failure(std::move(tree.error()));
    }

    if (Core::Status status = tree->setProductTheme(themeFor(config.initialTheme)); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setLayoutStyle(root->rootNodeId(), rootStyle()); !status)
    {
        return status;
    }
    const UI::UINodeId rootNode = root->rootNodeId();

    const auto addPanel = [&](Rect rect, UI::UINodeId& destination) -> Core::Status {
        return storeNode(createPanel(*tree, rootNode, rect), destination);
    };
    if (Core::Status status = addPanel({24.0F, 20.0F, 560.0F, 78.0F}, nodes_.headerPanel); !status)
    {
        return status;
    }
    if (Core::Status status = addPanel({24.0F, 20.0F, 6.0F, 78.0F}, nodes_.headerAccent); !status)
    {
        return status;
    }
    if (Core::Status status = addPanel({932.0F, 20.0F, 324.0F, 376.0F}, nodes_.inspectorPanel); !status)
    {
        return status;
    }
    if (Core::Status status = addPanel({932.0F, 20.0F, 6.0F, 376.0F}, nodes_.inspectorAccent); !status)
    {
        return status;
    }
    if (Core::Status status = addPanel({24.0F, 646.0F, 560.0F, 50.0F}, nodes_.statusPanel); !status)
    {
        return status;
    }

    const auto addLabel = [&](Rect rect, std::string_view text, UI::UINodeId& destination) -> Core::Status {
        return storeNode(createLabel(*tree, rootNode, rect, text), destination);
    };
    if (Core::Status status = addLabel({48.0F, 32.0F, 510.0F, 30.0F}, "Tina 3D Studio", nodes_.title); !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({48.0F, 65.0F, 510.0F, 22.0F}, "Cooked glTF / PBR Preview", nodes_.subtitle);
        !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({956.0F, 38.0F, 276.0F, 28.0F}, "Scene Controls", nodes_.inspectorTitle);
        !status)
    {
        return status;
    }
    if (Core::Status status =
            addLabel({956.0F, 69.0F, 276.0F, 22.0F}, "PBR materials | 3 lights", nodes_.inspectorMeta);
        !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({996.0F, 174.0F, 230.0F, 24.0F}, "Auto rotate", nodes_.autoRotateLabel); !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({956.0F, 215.0F, 276.0F, 24.0F}, "Rotation speed", nodes_.rotationSpeedLabel);
        !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({956.0F, 293.0F, 190.0F, 24.0F}, "Frame progress", nodes_.progressCaption);
        !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({1168.0F, 293.0F, 64.0F, 24.0F}, "0%", nodes_.progressValue); !status)
    {
        return status;
    }
    if (Core::Status status =
            addLabel({44.0F, 659.0F, 520.0F, 26.0F}, "Dark theme | Auto rotate | 1.00x", nodes_.status);
        !status)
    {
        return status;
    }

    if (Core::Status status = storeNode(
            createButton(*tree, rootNode, {956.0F, 105.0F, 276.0F, 42.0F},
                         config.initialTheme == Product3DUITheme::Dark ? "Switch to light" : "Switch to dark"),
            nodes_.themeButton);
        !status)
    {
        return status;
    }
    if (Core::Status status = tree->setButtonAction(
            nodes_.themeButton, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                ++evidence_->themeButtonActivations;
                requestedTheme_ = oppositeTheme(requestedTheme_.value_or(currentTheme_));
            }});
        !status)
    {
        return status;
    }

    auto checkbox = tree->createCheckbox(rootNode);
    if (!checkbox)
    {
        return Core::failure(std::move(checkbox.error()));
    }
    nodes_.autoRotateCheckbox = *checkbox;
    if (Core::Status status = tree->setLayoutStyle(*checkbox, absoluteStyle({956.0F, 170.0F, 28.0F, 28.0F})); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setChecked(*checkbox, true); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setCheckboxAction(
            *checkbox, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                ++evidence_->checkboxActivations;
                autoRotateDirty_ = true;
            }});
        !status)
    {
        return status;
    }

    auto slider = tree->createSlider(rootNode);
    if (!slider)
    {
        return Core::failure(std::move(slider.error()));
    }
    nodes_.rotationSpeedSlider = *slider;
    if (Core::Status status = tree->setLayoutStyle(*slider, absoluteStyle({956.0F, 245.0F, 276.0F, 32.0F})); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setSliderRange(*slider, 0.25F, 2.0F, 0.25F); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setSliderValue(*slider, 1.0F); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setSliderChangeCallback(
            *slider, UI::UISliderChangeCallback{[this](const UI::UISliderChangeEvent& event) noexcept {
                requestedRotationSpeed_ = event.value;
                ++evidence_->sliderChanges;
            }});
        !status)
    {
        return status;
    }

    auto progress = tree->createProgressBar(rootNode);
    if (!progress)
    {
        return Core::failure(std::move(progress.error()));
    }
    nodes_.frameProgress = *progress;
    if (Core::Status status = tree->setLayoutStyle(*progress, absoluteStyle({956.0F, 327.0F, 276.0F, 18.0F})); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setProgressBarRange(*progress, 0.0F, 100.0F); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setProgressBarValue(*progress, 0.0F); !status)
    {
        return status;
    }

    if (Core::Status status = applyTheme(*tree, config.initialTheme, false); !status)
    {
        return status;
    }
    if (Core::Status status = applyProgress(*tree, 0); !status)
    {
        return status;
    }
    if (Core::Status status = publishStatus(*tree); !status)
    {
        return status;
    }

    auto checked = tree->isChecked(nodes_.autoRotateCheckbox);
    auto speed = tree->sliderValue(nodes_.rotationSpeedSlider);
    auto progressValue = tree->progressBarValue(nodes_.frameProgress);
    if (!checked)
    {
        return Core::failure(std::move(checked.error()));
    }
    if (!speed)
    {
        return Core::failure(std::move(speed.error()));
    }
    if (!progressValue)
    {
        return Core::failure(std::move(progressValue.error()));
    }
    evidence_->controlsInitialStateVerified =
        *checked && std::abs(*speed - 1.0F) <= 0.0001F && std::abs(*progressValue) <= 0.0001F;
    if (!evidence_->controlsInitialStateVerified)
    {
        return Core::failure(Core::CoreErrorCode::Internal, "3D product UI initial control state verification failed");
    }

    evidence_->rootsCreated = 1;
    evidence_->panelsCreated = PanelCount;
    evidence_->labelsCreated = LabelCount;
    evidence_->buttonsCreated = 1;
    evidence_->checkboxesCreated = 1;
    evidence_->slidersCreated = 1;
    evidence_->progressBarsCreated = 1;
    evidence_->rootAlive = true;
    root_ = std::move(*root);
    return Core::success();
}

Core::Status Product3DUI::applyTheme(PrimaryWindowUITreeUpdater& tree, Product3DUITheme mode, bool countSwitch)
{
    const UI::UITheme& theme = themeFor(mode);
    if (Core::Status status = tree.setProductTheme(theme); !status)
    {
        return status;
    }

    if (Core::Status status =
            tree.setBoxPaint(nodes_.headerPanel, UI::makePanelBoxPaint(theme, UI::scaleColorAlpha(theme.surface1, 224),
                                                                       UI::UIElevation::Low));
        !status)
    {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(nodes_.headerAccent, UI::makeSolidBox(theme.accent)); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(
            nodes_.inspectorPanel,
            UI::makePanelBoxPaint(theme, UI::scaleColorAlpha(theme.surface0, 236), UI::UIElevation::Low));
        !status)
    {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(nodes_.inspectorAccent, UI::makeSolidBox(theme.textAccent)); !status)
    {
        return status;
    }
    if (Core::Status status =
            tree.setBoxPaint(nodes_.statusPanel, UI::makePanelBoxPaint(theme, UI::scaleColorAlpha(theme.surface2, 222),
                                                                       UI::UIElevation::None));
        !status)
    {
        return status;
    }

    if (Core::Status status = tree.setTextStyle(nodes_.title, UI::makeTitleTextStyle(theme, 27.0F)); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setTextStyle(nodes_.subtitle, UI::makeSecondaryTextStyle(theme, 16.0F)); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setTextStyle(nodes_.inspectorTitle, UI::makeTitleTextStyle(theme, 22.0F)); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setTextStyle(nodes_.inspectorMeta, UI::makeSecondaryTextStyle(theme, 15.0F));
        !status)
    {
        return status;
    }
    const std::array bodyLabels{nodes_.autoRotateLabel, nodes_.rotationSpeedLabel, nodes_.progressCaption};
    for (UI::UINodeId label : bodyLabels)
    {
        if (Core::Status status = tree.setTextStyle(label, UI::makeBodyTextStyle(theme, 17.0F)); !status)
        {
            return status;
        }
    }
    if (Core::Status status = tree.setTextStyle(nodes_.progressValue, UI::makeAccentTextStyle(theme, 17.0F)); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setTextStyle(nodes_.status, UI::makeSecondaryTextStyle(theme, 16.0F)); !status)
    {
        return status;
    }
    if (Core::Status status =
            tree.setText(nodes_.themeButton, mode == Product3DUITheme::Dark ? "Switch to light" : "Switch to dark");
        !status)
    {
        return status;
    }

    auto activeTheme = tree.productTheme();
    auto buttonPaint = tree.buttonPaint(nodes_.themeButton);
    auto checkboxPaint = tree.checkboxPaint(nodes_.autoRotateCheckbox);
    auto sliderPaint = tree.sliderPaint(nodes_.rotationSpeedSlider);
    auto progressPaint = tree.progressBarPaint(nodes_.frameProgress);
    if (!activeTheme)
    {
        return Core::failure(std::move(activeTheme.error()));
    }
    if (!buttonPaint)
    {
        return Core::failure(std::move(buttonPaint.error()));
    }
    if (!checkboxPaint)
    {
        return Core::failure(std::move(checkboxPaint.error()));
    }
    if (!sliderPaint)
    {
        return Core::failure(std::move(sliderPaint.error()));
    }
    if (!progressPaint)
    {
        return Core::failure(std::move(progressPaint.error()));
    }
    if (*activeTheme != theme || *buttonPaint != UI::makeButtonChrome(theme).states ||
        *checkboxPaint != UI::makeCheckboxChrome(theme).indicator ||
        *sliderPaint != UI::makeSliderChrome(theme).slider || *progressPaint != UI::makeProgressBarChrome(theme).bar)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "3D product controls did not inherit the requested UI Theme");
    }

    evidence_->inheritedChromeVerified = true;
    if (countSwitch && mode != currentTheme_)
    {
        ++evidence_->themeSwitches;
    }
    currentTheme_ = mode;
    evidence_->finalThemeLight = mode == Product3DUITheme::Light;
    statusDirty_ = true;
    return Core::success();
}

Core::Status Product3DUI::applyProgress(PrimaryWindowUITreeUpdater& tree, Core::u64 completedFrames)
{
    const double ratio = static_cast<double>((std::min)(completedFrames, config_.targetFrameCount)) /
                         static_cast<double>(config_.targetFrameCount);
    const float progress = static_cast<float>(ratio * 100.0);
    if (Core::Status status = tree.setProgressBarValue(nodes_.frameProgress, progress); !status)
    {
        return status;
    }

    const int percent = static_cast<int>(std::lround(progress));
    if (percent != lastProgressPercent_)
    {
        std::array<char, 16> text{};
        auto [end, error] = std::to_chars(text.data(), text.data() + text.size() - 1, percent);
        if (error != std::errc{})
        {
            return Core::failure(Core::CoreErrorCode::Internal, "3D product progress formatting failed");
        }
        *end++ = '%';
        if (Core::Status status = tree.setText(
                nodes_.progressValue, std::string_view{text.data(), static_cast<Core::usize>(end - text.data())});
            !status)
        {
            return status;
        }
        lastProgressPercent_ = percent;
    }

    ++evidence_->progressUpdates;
    evidence_->finalProgress = progress;
    return Core::success();
}

Core::Status Product3DUI::publishStatus(PrimaryWindowUITreeUpdater& tree)
{
    static constexpr std::array<std::string_view, 8> SpeedLabels{
        "0.25x", "0.50x", "0.75x", "1.00x", "1.25x", "1.50x", "1.75x", "2.00x",
    };
    const int speedIndex = std::clamp(static_cast<int>(std::lround((rotationSpeed_ - 0.25F) / 0.25F)), 0, 7);

    std::string text = currentTheme_ == Product3DUITheme::Dark ? "Dark theme | " : "Light theme | ";
    text += autoRotate_ ? "Auto rotate | " : "Rotation paused | ";
    text += SpeedLabels[static_cast<Core::usize>(speedIndex)];
    if (Core::Status status = tree.setText(nodes_.status, text); !status)
    {
        return status;
    }
    statusDirty_ = false;
    return Core::success();
}

Core::Status Product3DUI::update(UIUpdateContext& context, Core::u64 completedFrames)
{
    if (!root_ || !context.hasPrimaryWindowUI())
    {
        return Core::success();
    }

    const Core::u64 firstThemeFrame = (std::max)(Core::u64{1}, config_.targetFrameCount / Core::u64{3});
    const Core::u64 secondThemeFrame =
        (std::max)(firstThemeFrame + Core::u64{1}, config_.targetFrameCount - config_.targetFrameCount / Core::u64{3});
    if (config_.automatedThemeDemo && !firstAutomatedThemeStepQueued_ && completedFrames >= firstThemeFrame)
    {
        requestedTheme_ = oppositeTheme(config_.initialTheme);
        firstAutomatedThemeStepQueued_ = true;
        ++evidence_->automatedThemeSteps;
    } else if (config_.automatedThemeDemo && firstAutomatedThemeStepQueued_ && !secondAutomatedThemeStepQueued_ &&
               completedFrames >= secondThemeFrame)
    {
        requestedTheme_ = config_.initialTheme;
        secondAutomatedThemeStepQueued_ = true;
        ++evidence_->automatedThemeSteps;
    }

    auto tree = context.primaryWindowUITreeUpdater(root_);
    if (!tree)
    {
        return Core::failure(std::move(tree.error()));
    }

    if (requestedTheme_.has_value())
    {
        const Product3DUITheme requested = *requestedTheme_;
        requestedTheme_.reset();
        if (Core::Status status = applyTheme(*tree, requested, true); !status)
        {
            return status;
        }
    }
    if (requestedRotationSpeed_.has_value())
    {
        rotationSpeed_ = std::clamp(*requestedRotationSpeed_, 0.25F, 2.0F);
        requestedRotationSpeed_.reset();
        evidence_->rotationSpeed = rotationSpeed_;
        statusDirty_ = true;
    }
    if (autoRotateDirty_)
    {
        autoRotateDirty_ = false;
        auto checked = tree->isChecked(nodes_.autoRotateCheckbox);
        if (!checked)
        {
            return Core::failure(std::move(checked.error()));
        }
        autoRotate_ = *checked;
        evidence_->autoRotate = autoRotate_;
        statusDirty_ = true;
    }
    if (Core::Status status = applyProgress(*tree, completedFrames); !status)
    {
        return status;
    }
    if (statusDirty_)
    {
        if (Core::Status status = publishStatus(*tree); !status)
        {
            return status;
        }
    }
    return Core::success();
}

void Product3DUI::release() noexcept
{
    if (!root_)
    {
        return;
    }
    root_.reset();
    evidence_->rootAlive = false;
    ++evidence_->rootsReleased;
}

} // namespace Tina::Sample3D
