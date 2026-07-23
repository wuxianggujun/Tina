#include <tina/runtime/PrimaryWindowUI.hpp>

#include "ui/PrimaryWindowUICapabilityState.hpp"

#include <tina/runtime/RuntimeErrors.hpp>

#include <string_view>
#include <utility>

namespace Tina {
namespace {

template <typename Value> [[nodiscard]] Core::Result<Value> expiredFacade(std::string_view operation)
{
    Core::Error error{RuntimeErrorCode::UIPhaseCapabilityExpired, "The primary-window UI capability has expired"};
    error.addContext(operation, "moved-from or detached facade");
    return Core::failure(std::move(error));
}

} // namespace

PrimaryWindowUITreeUpdater::PrimaryWindowUITreeUpdater(Runtime::Detail::PrimaryWindowUICapabilityState& state,
                                                       u64 epoch, Runtime::Detail::PrimaryWindowUIPhase phase,
                                                       UI::UITreeUpdater updater) noexcept
    : m_state(&state), m_epoch(epoch), m_phase(phase), m_updater(std::move(updater))
{
}

PrimaryWindowUITreeUpdater::PrimaryWindowUITreeUpdater(PrimaryWindowUITreeUpdater&& other) noexcept
    : m_state(std::exchange(other.m_state, nullptr)), m_epoch(std::exchange(other.m_epoch, 0)),
      m_phase(std::exchange(other.m_phase, Runtime::Detail::PrimaryWindowUIPhase::None)),
      m_updater(std::move(other.m_updater))
{
}

PrimaryWindowUITreeUpdater& PrimaryWindowUITreeUpdater::operator=(PrimaryWindowUITreeUpdater&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    m_state = std::exchange(other.m_state, nullptr);
    m_epoch = std::exchange(other.m_epoch, 0);
    m_phase = std::exchange(other.m_phase, Runtime::Detail::PrimaryWindowUIPhase::None);
    m_updater = std::move(other.m_updater);
    return *this;
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isAlive(UI::UINodeId node) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isAlive");
    }
    return m_state->isAlive(m_epoch, m_phase, m_updater, node);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::createPanel(UI::UINodeId parent)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::createPanel");
    }
    return m_state->createPanel(m_epoch, m_phase, m_updater, parent);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::createLabel(UI::UINodeId parent)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::createLabel");
    }
    return m_state->createLabel(m_epoch, m_phase, m_updater, parent);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::createTextEdit(UI::UINodeId parent)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::createTextEdit");
    }
    return m_state->createTextEdit(m_epoch, m_phase, m_updater, parent);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::createButton(UI::UINodeId parent)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::createButton");
    }
    return m_state->createButton(m_epoch, m_phase, m_updater, parent);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::createCheckbox(UI::UINodeId parent)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::createCheckbox");
    }
    return m_state->createCheckbox(m_epoch, m_phase, m_updater, parent);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::createSlider(UI::UINodeId parent)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::createSlider");
    }
    return m_state->createSlider(m_epoch, m_phase, m_updater, parent);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::createProgressBar(UI::UINodeId parent)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::createProgressBar");
    }
    return m_state->createProgressBar(m_epoch, m_phase, m_updater, parent);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::createRadioButton(UI::UINodeId parent)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::createRadioButton");
    }
    return m_state->createRadioButton(m_epoch, m_phase, m_updater, parent);
}

Core::Status PrimaryWindowUITreeUpdater::setLayoutStyle(UI::UINodeId node, const UI::UILayoutStyle& style)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setLayoutStyle");
    }
    return m_state->setLayoutStyle(m_epoch, m_phase, m_updater, node, style);
}

Core::Status PrimaryWindowUITreeUpdater::setPointerHitPolicy(UI::UINodeId node, UI::UIPointerHitPolicy policy)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setPointerHitPolicy");
    }
    return m_state->setPointerHitPolicy(m_epoch, m_phase, m_updater, node, policy);
}

Core::Status PrimaryWindowUITreeUpdater::setEnabled(UI::UINodeId node, bool enabled)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setEnabled");
    }
    return m_state->setEnabled(m_epoch, m_phase, m_updater, node, enabled);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isEnabled(UI::UINodeId node) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isEnabled");
    }
    return m_state->isEnabled(m_epoch, m_phase, m_updater, node);
}

Core::Status PrimaryWindowUITreeUpdater::setBoxPaint(UI::UINodeId node, const UI::UIBoxPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setBoxPaint");
    }
    return m_state->setBoxPaint(m_epoch, m_phase, m_updater, node, paint);
}

Core::Status PrimaryWindowUITreeUpdater::setButtonPaint(
    UI::UINodeId button,
    const UI::UIButtonPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setButtonPaint");
    }
    return m_state->setButtonPaint(m_epoch, m_phase, m_updater, button, paint);
}

Core::Result<UI::UIButtonPaint> PrimaryWindowUITreeUpdater::buttonPaint(
    UI::UINodeId button) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIButtonPaint>("PrimaryWindowUITreeUpdater::buttonPaint");
    }
    return m_state->buttonPaint(m_epoch, m_phase, m_updater, button);
}

Core::Status PrimaryWindowUITreeUpdater::setText(UI::UINodeId node, std::string_view utf8)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setText");
    }
    return m_state->setText(m_epoch, m_phase, m_updater, node, utf8);
}

Core::Status PrimaryWindowUITreeUpdater::setTextStyle(UI::UINodeId node, const UI::UITextStyle& style)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTextStyle");
    }
    return m_state->setTextStyle(m_epoch, m_phase, m_updater, node, style);
}

Core::Result<std::string_view> PrimaryWindowUITreeUpdater::text(UI::UINodeId node)
{
    if (m_state == nullptr)
    {
        return expiredFacade<std::string_view>("PrimaryWindowUITreeUpdater::text");
    }
    return m_state->text(m_epoch, m_phase, m_updater, node);
}

Core::Result<UI::UITextStyle> PrimaryWindowUITreeUpdater::textStyle(UI::UINodeId node)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITextStyle>("PrimaryWindowUITreeUpdater::textStyle");
    }
    return m_state->textStyle(m_epoch, m_phase, m_updater, node);
}

Core::Status PrimaryWindowUITreeUpdater::setTextSelection(UI::UINodeId textEdit, UI::UITextSelection selection)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTextSelection");
    }
    return m_state->setTextSelection(m_epoch, m_phase, m_updater, textEdit, selection);
}

Core::Result<UI::UITextSelection> PrimaryWindowUITreeUpdater::textSelection(UI::UINodeId textEdit) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITextSelection>("PrimaryWindowUITreeUpdater::textSelection");
    }
    return m_state->textSelection(m_epoch, m_phase, m_updater, textEdit);
}

Core::Status PrimaryWindowUITreeUpdater::setButtonAction(UI::UINodeId button, UI::UIButtonActionCallback callback)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setButtonAction");
    }
    return m_state->setButtonAction(m_epoch, m_phase, m_updater, button, std::move(callback));
}

Core::Status PrimaryWindowUITreeUpdater::clearButtonAction(UI::UINodeId button)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearButtonAction");
    }
    return m_state->clearButtonAction(m_epoch, m_phase, m_updater, button);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isButtonPressed(UI::UINodeId button) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isButtonPressed");
    }
    return m_state->isButtonPressed(m_epoch, m_phase, m_updater, button);
}

Core::Status PrimaryWindowUITreeUpdater::setCheckboxAction(UI::UINodeId checkbox, UI::UIButtonActionCallback callback)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setCheckboxAction");
    }
    return m_state->setCheckboxAction(m_epoch, m_phase, m_updater, checkbox, std::move(callback));
}

Core::Status PrimaryWindowUITreeUpdater::clearCheckboxAction(UI::UINodeId checkbox)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearCheckboxAction");
    }
    return m_state->clearCheckboxAction(m_epoch, m_phase, m_updater, checkbox);
}

Core::Status PrimaryWindowUITreeUpdater::setCheckboxPaint(UI::UINodeId checkbox,
                                                           const UI::UICheckboxPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setCheckboxPaint");
    }
    return m_state->setCheckboxPaint(m_epoch, m_phase, m_updater, checkbox, paint);
}

Core::Result<UI::UICheckboxPaint> PrimaryWindowUITreeUpdater::checkboxPaint(UI::UINodeId checkbox) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UICheckboxPaint>("PrimaryWindowUITreeUpdater::checkboxPaint");
    }
    return m_state->checkboxPaint(m_epoch, m_phase, m_updater, checkbox);
}

Core::Status PrimaryWindowUITreeUpdater::setChecked(UI::UINodeId checkbox, bool checked)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setChecked");
    }
    return m_state->setChecked(m_epoch, m_phase, m_updater, checkbox, checked);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isChecked(UI::UINodeId checkbox) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isChecked");
    }
    return m_state->isChecked(m_epoch, m_phase, m_updater, checkbox);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isCheckboxPressed(UI::UINodeId checkbox) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isCheckboxPressed");
    }
    return m_state->isCheckboxPressed(m_epoch, m_phase, m_updater, checkbox);
}

Core::Status PrimaryWindowUITreeUpdater::setSliderRange(UI::UINodeId slider, float minValue, float maxValue,
                                                        float step)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setSliderRange");
    }
    return m_state->setSliderRange(m_epoch, m_phase, m_updater, slider, minValue, maxValue, step);
}

Core::Status PrimaryWindowUITreeUpdater::setSliderValue(UI::UINodeId slider, float value)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setSliderValue");
    }
    return m_state->setSliderValue(m_epoch, m_phase, m_updater, slider, value);
}

Core::Result<float> PrimaryWindowUITreeUpdater::sliderValue(UI::UINodeId slider) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<float>("PrimaryWindowUITreeUpdater::sliderValue");
    }
    return m_state->sliderValue(m_epoch, m_phase, m_updater, slider);
}

Core::Status PrimaryWindowUITreeUpdater::setSliderPaint(
    UI::UINodeId slider,
    const UI::UISliderPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setSliderPaint");
    }
    return m_state->setSliderPaint(m_epoch, m_phase, m_updater, slider, paint);
}

Core::Result<UI::UISliderPaint> PrimaryWindowUITreeUpdater::sliderPaint(
    UI::UINodeId slider) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UISliderPaint>("PrimaryWindowUITreeUpdater::sliderPaint");
    }
    return m_state->sliderPaint(m_epoch, m_phase, m_updater, slider);
}

Core::Status PrimaryWindowUITreeUpdater::setSliderChangeCallback(UI::UINodeId slider,
                                                                 UI::UISliderChangeCallback callback)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setSliderChangeCallback");
    }
    return m_state->setSliderChangeCallback(m_epoch, m_phase, m_updater, slider, std::move(callback));
}

Core::Status PrimaryWindowUITreeUpdater::clearSliderChangeCallback(UI::UINodeId slider)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearSliderChangeCallback");
    }
    return m_state->clearSliderChangeCallback(m_epoch, m_phase, m_updater, slider);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isSliderDragging(UI::UINodeId slider) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isSliderDragging");
    }
    return m_state->isSliderDragging(m_epoch, m_phase, m_updater, slider);
}

Core::Status PrimaryWindowUITreeUpdater::setProgressBarRange(
    UI::UINodeId progressBar, float minValue, float maxValue)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setProgressBarRange");
    }
    return m_state->setProgressBarRange(
        m_epoch, m_phase, m_updater, progressBar, minValue, maxValue);
}

Core::Status PrimaryWindowUITreeUpdater::setProgressBarValue(
    UI::UINodeId progressBar, float value)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setProgressBarValue");
    }
    return m_state->setProgressBarValue(m_epoch, m_phase, m_updater, progressBar, value);
}

Core::Result<float> PrimaryWindowUITreeUpdater::progressBarValue(
    UI::UINodeId progressBar) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<float>("PrimaryWindowUITreeUpdater::progressBarValue");
    }
    return m_state->progressBarValue(m_epoch, m_phase, m_updater, progressBar);
}

Core::Status PrimaryWindowUITreeUpdater::setProgressBarPaint(
    UI::UINodeId progressBar, const UI::UIProgressBarPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setProgressBarPaint");
    }
    return m_state->setProgressBarPaint(m_epoch, m_phase, m_updater, progressBar, paint);
}

Core::Result<UI::UIProgressBarPaint> PrimaryWindowUITreeUpdater::progressBarPaint(
    UI::UINodeId progressBar) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIProgressBarPaint>("PrimaryWindowUITreeUpdater::progressBarPaint");
    }
    return m_state->progressBarPaint(m_epoch, m_phase, m_updater, progressBar);
}

Core::Status PrimaryWindowUITreeUpdater::setRadioButtonPaint(
    UI::UINodeId radioButton, const UI::UIRadioButtonPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setRadioButtonPaint");
    }
    return m_state->setRadioButtonPaint(m_epoch, m_phase, m_updater, radioButton, paint);
}

Core::Result<UI::UIRadioButtonPaint> PrimaryWindowUITreeUpdater::radioButtonPaint(
    UI::UINodeId radioButton) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIRadioButtonPaint>("PrimaryWindowUITreeUpdater::radioButtonPaint");
    }
    return m_state->radioButtonPaint(m_epoch, m_phase, m_updater, radioButton);
}

Core::Status PrimaryWindowUITreeUpdater::setRadioButtonAction(
    UI::UINodeId radioButton, UI::UIButtonActionCallback callback)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setRadioButtonAction");
    }
    return m_state->setRadioButtonAction(
        m_epoch, m_phase, m_updater, radioButton, std::move(callback));
}

Core::Status PrimaryWindowUITreeUpdater::clearRadioButtonAction(UI::UINodeId radioButton)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearRadioButtonAction");
    }
    return m_state->clearRadioButtonAction(m_epoch, m_phase, m_updater, radioButton);
}

Core::Status PrimaryWindowUITreeUpdater::setRadioButtonSelected(
    UI::UINodeId radioButton, bool selected)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setRadioButtonSelected");
    }
    return m_state->setRadioButtonSelected(m_epoch, m_phase, m_updater, radioButton, selected);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isRadioButtonSelected(
    UI::UINodeId radioButton) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isRadioButtonSelected");
    }
    return m_state->isRadioButtonSelected(m_epoch, m_phase, m_updater, radioButton);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isRadioButtonPressed(
    UI::UINodeId radioButton) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isRadioButtonPressed");
    }
    return m_state->isRadioButtonPressed(m_epoch, m_phase, m_updater, radioButton);
}

Core::Result<UI::UIRoutedPointerListenerToken>
PrimaryWindowUITreeUpdater::addRoutedPointerListener(UI::UIRoutedPointerListenerDesc descriptor,
                                                     UI::UIRoutedPointerCallback callback)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIRoutedPointerListenerToken>("PrimaryWindowUITreeUpdater::addRoutedPointerListener");
    }
    return m_state->addRoutedPointerListener(m_epoch, m_phase, m_updater, descriptor, std::move(callback));
}

Core::Status PrimaryWindowUITreeUpdater::destroy(UI::UINodeId node)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::destroy");
    }
    return m_state->destroy(m_epoch, m_phase, m_updater, node);
}

PrimaryWindowUIRootBuilder::PrimaryWindowUIRootBuilder(Runtime::Detail::PrimaryWindowUICapabilityState& state,
                                                       u64 epoch) noexcept
    : m_state(&state), m_epoch(epoch)
{
}

PrimaryWindowUIRootBuilder::PrimaryWindowUIRootBuilder(PrimaryWindowUIRootBuilder&& other) noexcept
    : m_state(std::exchange(other.m_state, nullptr)), m_epoch(std::exchange(other.m_epoch, 0))
{
}

PrimaryWindowUIRootBuilder& PrimaryWindowUIRootBuilder::operator=(PrimaryWindowUIRootBuilder&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    m_state = std::exchange(other.m_state, nullptr);
    m_epoch = std::exchange(other.m_epoch, 0);
    return *this;
}

Core::Result<UI::UIRootOwner> PrimaryWindowUIRootBuilder::createRoot()
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIRootOwner>("PrimaryWindowUIRootBuilder::createRoot");
    }
    return m_state->createRoot(m_epoch);
}

Core::Result<PrimaryWindowUITreeUpdater> PrimaryWindowUIRootBuilder::treeUpdater(UI::UIRootOwner& rootOwner)
{
    if (m_state == nullptr)
    {
        return expiredFacade<PrimaryWindowUITreeUpdater>("PrimaryWindowUIRootBuilder::treeUpdater");
    }
    return m_state->treeUpdater(m_epoch, Runtime::Detail::PrimaryWindowUIPhase::GameStateEnter, rootOwner);
}

} // namespace Tina
