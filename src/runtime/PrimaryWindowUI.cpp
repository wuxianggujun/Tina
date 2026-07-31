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

Core::Result<UI::UINodeId>
PrimaryWindowUITreeUpdater::createElement(UI::UINodeId parent, const UI::UIElementDescriptor& descriptor)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::createElement");
    }
    return m_state->createElement(m_epoch, m_phase, m_updater, parent, descriptor);
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

Core::Status PrimaryWindowUITreeUpdater::setFocusScopeMode(UI::UINodeId node, UI::UIFocusScopeMode mode)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setFocusScopeMode");
    }
    return m_state->setFocusScopeMode(m_epoch, m_phase, m_updater, node, mode);
}

Core::Result<UI::UIFocusScopeMode> PrimaryWindowUITreeUpdater::focusScopeMode(UI::UINodeId node) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIFocusScopeMode>("PrimaryWindowUITreeUpdater::focusScopeMode");
    }
    return m_state->focusScopeMode(m_epoch, m_phase, m_updater, node);
}

Core::Status PrimaryWindowUITreeUpdater::requestFocus(UI::UINodeId node)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::requestFocus");
    }
    return m_state->requestFocus(m_epoch, m_phase, m_updater, node);
}

Core::Status PrimaryWindowUITreeUpdater::clearFocus()
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearFocus");
    }
    return m_state->clearFocus(m_epoch, m_phase, m_updater);
}

Core::Status PrimaryWindowUITreeUpdater::setStyleRole(UI::UINodeId node, UI::UIStyleRoleId role)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setStyleRole");
    }
    return m_state->setStyleRole(m_epoch, m_phase, m_updater, node, role);
}

Core::Result<UI::UIStyleRoleId> PrimaryWindowUITreeUpdater::styleRole(UI::UINodeId node) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIStyleRoleId>("PrimaryWindowUITreeUpdater::styleRole");
    }
    return m_state->styleRole(m_epoch, m_phase, m_updater, node);
}

Core::Status PrimaryWindowUITreeUpdater::clearOverride(UI::UINodeId node, UI::UIStyleOverride properties)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearOverride");
    }
    return m_state->clearOverride(m_epoch, m_phase, m_updater, node, properties);
}

Core::Result<UI::UITheme> PrimaryWindowUITreeUpdater::productTheme() const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITheme>("PrimaryWindowUITreeUpdater::productTheme");
    }
    return m_state->productTheme(m_epoch, m_phase);
}

Core::Status PrimaryWindowUITreeUpdater::setProductTheme(const UI::UITheme& theme)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setProductTheme");
    }
    return m_state->setProductTheme(m_epoch, m_phase, theme);
}

Core::Status PrimaryWindowUITreeUpdater::setBoxPaint(UI::UINodeId node, const UI::UIBoxPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setBoxPaint");
    }
    return m_state->setBoxPaint(m_epoch, m_phase, m_updater, node, paint);
}

Core::Status PrimaryWindowUITreeUpdater::setButtonPaint(UI::UINodeId button, const UI::UIButtonPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setButtonPaint");
    }
    return m_state->setButtonPaint(m_epoch, m_phase, m_updater, button, paint);
}

Core::Result<UI::UIButtonPaint> PrimaryWindowUITreeUpdater::buttonPaint(UI::UINodeId button) const
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

Core::Status PrimaryWindowUITreeUpdater::setContentAlignment(UI::UINodeId node,
                                                              UI::UIContentAlignment alignment)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setContentAlignment");
    }
    return m_state->setContentAlignment(m_epoch, m_phase, m_updater, node, alignment);
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

Core::Result<UI::UIContentAlignment> PrimaryWindowUITreeUpdater::contentAlignment(UI::UINodeId node) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIContentAlignment>("PrimaryWindowUITreeUpdater::contentAlignment");
    }
    return m_state->contentAlignment(m_epoch, m_phase, m_updater, node);
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

Core::Status PrimaryWindowUITreeUpdater::setTextEditPaint(UI::UINodeId textEdit,
                                                          const UI::UITextEditPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTextEditPaint");
    }
    return m_state->setTextEditPaint(m_epoch, m_phase, m_updater, textEdit, paint);
}

Core::Result<UI::UITextEditPaint> PrimaryWindowUITreeUpdater::textEditPaint(UI::UINodeId textEdit) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITextEditPaint>("PrimaryWindowUITreeUpdater::textEditPaint");
    }
    return m_state->textEditPaint(m_epoch, m_phase, m_updater, textEdit);
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

Core::Status PrimaryWindowUITreeUpdater::setCheckboxPaint(UI::UINodeId checkbox, const UI::UICheckboxPaint& paint)
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

Core::Status PrimaryWindowUITreeUpdater::setSliderRange(UI::UINodeId slider, float minValue, float maxValue, float step)
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

Core::Status PrimaryWindowUITreeUpdater::setSliderPaint(UI::UINodeId slider, const UI::UISliderPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setSliderPaint");
    }
    return m_state->setSliderPaint(m_epoch, m_phase, m_updater, slider, paint);
}

Core::Result<UI::UISliderPaint> PrimaryWindowUITreeUpdater::sliderPaint(UI::UINodeId slider) const
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

Core::Status PrimaryWindowUITreeUpdater::setScrollViewStyle(UI::UINodeId scrollView,
                                                           const UI::UIScrollViewStyle& style)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setScrollViewStyle");
    }
    return m_state->setScrollViewStyle(m_epoch, m_phase, m_updater, scrollView, style);
}

Core::Result<UI::UIScrollViewStyle> PrimaryWindowUITreeUpdater::scrollViewStyle(UI::UINodeId scrollView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIScrollViewStyle>("PrimaryWindowUITreeUpdater::scrollViewStyle");
    }
    return m_state->scrollViewStyle(m_epoch, m_phase, m_updater, scrollView);
}

Core::Status PrimaryWindowUITreeUpdater::setScrollViewOffset(UI::UINodeId scrollView, UI::UIScrollOffset offset)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setScrollViewOffset");
    }
    return m_state->setScrollViewOffset(m_epoch, m_phase, m_updater, scrollView, offset);
}

Core::Result<UI::UIScrollOffset> PrimaryWindowUITreeUpdater::scrollViewOffset(UI::UINodeId scrollView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIScrollOffset>("PrimaryWindowUITreeUpdater::scrollViewOffset");
    }
    return m_state->scrollViewOffset(m_epoch, m_phase, m_updater, scrollView);
}

Core::Result<UI::UIScrollViewMetrics> PrimaryWindowUITreeUpdater::scrollViewMetrics(UI::UINodeId scrollView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIScrollViewMetrics>("PrimaryWindowUITreeUpdater::scrollViewMetrics");
    }
    return m_state->scrollViewMetrics(m_epoch, m_phase, m_updater, scrollView);
}

Core::Status PrimaryWindowUITreeUpdater::setScrollViewPaint(UI::UINodeId scrollView,
                                                           const UI::UIScrollViewPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setScrollViewPaint");
    }
    return m_state->setScrollViewPaint(m_epoch, m_phase, m_updater, scrollView, paint);
}

Core::Result<UI::UIScrollViewPaint> PrimaryWindowUITreeUpdater::scrollViewPaint(UI::UINodeId scrollView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIScrollViewPaint>("PrimaryWindowUITreeUpdater::scrollViewPaint");
    }
    return m_state->scrollViewPaint(m_epoch, m_phase, m_updater, scrollView);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isScrollViewDragging(UI::UINodeId scrollView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isScrollViewDragging");
    }
    return m_state->isScrollViewDragging(m_epoch, m_phase, m_updater, scrollView);
}

Core::Status PrimaryWindowUITreeUpdater::setListViewDataSource(UI::UINodeId listView,
                                                               UI::UIListViewDataSource source)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setListViewDataSource");
    }
    return m_state->setListViewDataSource(m_epoch, m_phase, m_updater, listView, source);
}

Core::Status PrimaryWindowUITreeUpdater::clearListViewDataSource(UI::UINodeId listView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearListViewDataSource");
    }
    return m_state->clearListViewDataSource(m_epoch, m_phase, m_updater, listView);
}

Core::Status PrimaryWindowUITreeUpdater::invalidateListViewItems(UI::UINodeId listView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::invalidateListViewItems");
    }
    return m_state->invalidateListViewItems(m_epoch, m_phase, m_updater, listView);
}

Core::Status PrimaryWindowUITreeUpdater::setListViewStyle(UI::UINodeId listView,
                                                          const UI::UIListViewStyle& style)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setListViewStyle");
    }
    return m_state->setListViewStyle(m_epoch, m_phase, m_updater, listView, style);
}

Core::Result<UI::UIListViewStyle> PrimaryWindowUITreeUpdater::listViewStyle(UI::UINodeId listView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIListViewStyle>("PrimaryWindowUITreeUpdater::listViewStyle");
    }
    return m_state->listViewStyle(m_epoch, m_phase, m_updater, listView);
}

Core::Status PrimaryWindowUITreeUpdater::setListViewPaint(UI::UINodeId listView,
                                                          const UI::UIListViewPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setListViewPaint");
    }
    return m_state->setListViewPaint(m_epoch, m_phase, m_updater, listView, paint);
}

Core::Result<UI::UIListViewPaint> PrimaryWindowUITreeUpdater::listViewPaint(UI::UINodeId listView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIListViewPaint>("PrimaryWindowUITreeUpdater::listViewPaint");
    }
    return m_state->listViewPaint(m_epoch, m_phase, m_updater, listView);
}

Core::Result<UI::UIListViewMetrics> PrimaryWindowUITreeUpdater::listViewMetrics(UI::UINodeId listView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIListViewMetrics>("PrimaryWindowUITreeUpdater::listViewMetrics");
    }
    return m_state->listViewMetrics(m_epoch, m_phase, m_updater, listView);
}

Core::Status PrimaryWindowUITreeUpdater::setListViewSelectedIndex(UI::UINodeId listView, u64 logicalIndex)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setListViewSelectedIndex");
    }
    return m_state->setListViewSelectedIndex(m_epoch, m_phase, m_updater, listView, logicalIndex);
}

Core::Status PrimaryWindowUITreeUpdater::clearListViewSelection(UI::UINodeId listView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearListViewSelection");
    }
    return m_state->clearListViewSelection(m_epoch, m_phase, m_updater, listView);
}

Core::Result<UI::UIListViewSelection> PrimaryWindowUITreeUpdater::listViewSelection(UI::UINodeId listView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIListViewSelection>("PrimaryWindowUITreeUpdater::listViewSelection");
    }
    return m_state->listViewSelection(m_epoch, m_phase, m_updater, listView);
}

Core::Status PrimaryWindowUITreeUpdater::scrollListViewToIndex(UI::UINodeId listView, u64 logicalIndex,
                                                               UI::UIListViewScrollAlignment alignment)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::scrollListViewToIndex");
    }
    return m_state->scrollListViewToIndex(m_epoch, m_phase, m_updater, listView, logicalIndex, alignment);
}

Core::Status PrimaryWindowUITreeUpdater::setTreeViewDataSource(UI::UINodeId treeView,
                                                               UI::UITreeViewDataSource source)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTreeViewDataSource");
    }
    return m_state->setTreeViewDataSource(m_epoch, m_phase, m_updater, treeView, source);
}

Core::Status PrimaryWindowUITreeUpdater::clearTreeViewDataSource(UI::UINodeId treeView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearTreeViewDataSource");
    }
    return m_state->clearTreeViewDataSource(m_epoch, m_phase, m_updater, treeView);
}

Core::Status PrimaryWindowUITreeUpdater::invalidateTreeViewItems(UI::UINodeId treeView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::invalidateTreeViewItems");
    }
    return m_state->invalidateTreeViewItems(m_epoch, m_phase, m_updater, treeView);
}

Core::Status PrimaryWindowUITreeUpdater::setTreeViewStyle(UI::UINodeId treeView,
                                                          const UI::UITreeViewStyle& style)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTreeViewStyle");
    }
    return m_state->setTreeViewStyle(m_epoch, m_phase, m_updater, treeView, style);
}

Core::Result<UI::UITreeViewStyle> PrimaryWindowUITreeUpdater::treeViewStyle(UI::UINodeId treeView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITreeViewStyle>("PrimaryWindowUITreeUpdater::treeViewStyle");
    }
    return m_state->treeViewStyle(m_epoch, m_phase, m_updater, treeView);
}

Core::Status PrimaryWindowUITreeUpdater::setTreeViewPaint(UI::UINodeId treeView,
                                                          const UI::UITreeViewPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTreeViewPaint");
    }
    return m_state->setTreeViewPaint(m_epoch, m_phase, m_updater, treeView, paint);
}

Core::Result<UI::UITreeViewPaint> PrimaryWindowUITreeUpdater::treeViewPaint(UI::UINodeId treeView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITreeViewPaint>("PrimaryWindowUITreeUpdater::treeViewPaint");
    }
    return m_state->treeViewPaint(m_epoch, m_phase, m_updater, treeView);
}

Core::Result<UI::UITreeViewMetrics> PrimaryWindowUITreeUpdater::treeViewMetrics(UI::UINodeId treeView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITreeViewMetrics>("PrimaryWindowUITreeUpdater::treeViewMetrics");
    }
    return m_state->treeViewMetrics(m_epoch, m_phase, m_updater, treeView);
}

Core::Status PrimaryWindowUITreeUpdater::setTreeViewSelectedIndex(UI::UINodeId treeView, u64 logicalIndex)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTreeViewSelectedIndex");
    }
    return m_state->setTreeViewSelectedIndex(m_epoch, m_phase, m_updater, treeView, logicalIndex);
}

Core::Status PrimaryWindowUITreeUpdater::clearTreeViewSelection(UI::UINodeId treeView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearTreeViewSelection");
    }
    return m_state->clearTreeViewSelection(m_epoch, m_phase, m_updater, treeView);
}

Core::Result<UI::UITreeViewSelection> PrimaryWindowUITreeUpdater::treeViewSelection(UI::UINodeId treeView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITreeViewSelection>("PrimaryWindowUITreeUpdater::treeViewSelection");
    }
    return m_state->treeViewSelection(m_epoch, m_phase, m_updater, treeView);
}

Core::Status PrimaryWindowUITreeUpdater::setTreeViewItemExpanded(UI::UINodeId treeView, u64 logicalIndex,
                                                                 bool expanded)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTreeViewItemExpanded");
    }
    return m_state->setTreeViewItemExpanded(m_epoch, m_phase, m_updater, treeView, logicalIndex, expanded);
}

Core::Status PrimaryWindowUITreeUpdater::scrollTreeViewToIndex(UI::UINodeId treeView, u64 logicalIndex,
                                                               UI::UITreeViewScrollAlignment alignment)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::scrollTreeViewToIndex");
    }
    return m_state->scrollTreeViewToIndex(m_epoch, m_phase, m_updater, treeView, logicalIndex, alignment);
}

Core::Status PrimaryWindowUITreeUpdater::setPopupStyle(UI::UINodeId popup, const UI::UIPopupStyle& style)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setPopupStyle");
    }
    return m_state->setPopupStyle(m_epoch, m_phase, m_updater, popup, style);
}

Core::Result<UI::UIPopupStyle> PrimaryWindowUITreeUpdater::popupStyle(UI::UINodeId popup) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIPopupStyle>("PrimaryWindowUITreeUpdater::popupStyle");
    }
    return m_state->popupStyle(m_epoch, m_phase, m_updater, popup);
}

Core::Status PrimaryWindowUITreeUpdater::setPopupOpen(UI::UINodeId popup, bool open)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setPopupOpen");
    }
    return m_state->setPopupOpen(m_epoch, m_phase, m_updater, popup, open);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isPopupOpen(UI::UINodeId popup) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isPopupOpen");
    }
    return m_state->isPopupOpen(m_epoch, m_phase, m_updater, popup);
}

Core::Result<UI::UIPopupMetrics> PrimaryWindowUITreeUpdater::popupMetrics(UI::UINodeId popup) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIPopupMetrics>("PrimaryWindowUITreeUpdater::popupMetrics");
    }
    return m_state->popupMetrics(m_epoch, m_phase, m_updater, popup);
}

Core::Status PrimaryWindowUITreeUpdater::setDropdownOpen(UI::UINodeId dropdown, bool open)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setDropdownOpen");
    }
    return m_state->setDropdownOpen(m_epoch, m_phase, m_updater, dropdown, open);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isDropdownOpen(UI::UINodeId dropdown) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isDropdownOpen");
    }
    return m_state->isDropdownOpen(m_epoch, m_phase, m_updater, dropdown);
}

Core::Status PrimaryWindowUITreeUpdater::setDropdownSelectedItem(UI::UINodeId dropdown, UI::UINodeId item)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setDropdownSelectedItem");
    }
    return m_state->setDropdownSelectedItem(m_epoch, m_phase, m_updater, dropdown, item);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::dropdownSelectedItem(UI::UINodeId dropdown) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::dropdownSelectedItem");
    }
    return m_state->dropdownSelectedItem(m_epoch, m_phase, m_updater, dropdown);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isDropdownItemSelected(UI::UINodeId item) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isDropdownItemSelected");
    }
    return m_state->isDropdownItemSelected(m_epoch, m_phase, m_updater, item);
}

Core::Status PrimaryWindowUITreeUpdater::setDropdownPaint(UI::UINodeId dropdown,
                                                          const UI::UIDropdownPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setDropdownPaint");
    }
    return m_state->setDropdownPaint(m_epoch, m_phase, m_updater, dropdown, paint);
}

Core::Result<UI::UIDropdownPaint> PrimaryWindowUITreeUpdater::dropdownPaint(UI::UINodeId dropdown) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIDropdownPaint>("PrimaryWindowUITreeUpdater::dropdownPaint");
    }
    return m_state->dropdownPaint(m_epoch, m_phase, m_updater, dropdown);
}

Core::Status PrimaryWindowUITreeUpdater::setProgressBarRange(UI::UINodeId progressBar, float minValue, float maxValue)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setProgressBarRange");
    }
    return m_state->setProgressBarRange(m_epoch, m_phase, m_updater, progressBar, minValue, maxValue);
}

Core::Status PrimaryWindowUITreeUpdater::setProgressBarValue(UI::UINodeId progressBar, float value)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setProgressBarValue");
    }
    return m_state->setProgressBarValue(m_epoch, m_phase, m_updater, progressBar, value);
}

Core::Result<float> PrimaryWindowUITreeUpdater::progressBarValue(UI::UINodeId progressBar) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<float>("PrimaryWindowUITreeUpdater::progressBarValue");
    }
    return m_state->progressBarValue(m_epoch, m_phase, m_updater, progressBar);
}

Core::Status PrimaryWindowUITreeUpdater::setProgressBarPaint(UI::UINodeId progressBar,
                                                             const UI::UIProgressBarPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setProgressBarPaint");
    }
    return m_state->setProgressBarPaint(m_epoch, m_phase, m_updater, progressBar, paint);
}

Core::Result<UI::UIProgressBarPaint> PrimaryWindowUITreeUpdater::progressBarPaint(UI::UINodeId progressBar) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIProgressBarPaint>("PrimaryWindowUITreeUpdater::progressBarPaint");
    }
    return m_state->progressBarPaint(m_epoch, m_phase, m_updater, progressBar);
}

Core::Status PrimaryWindowUITreeUpdater::setRadioButtonPaint(UI::UINodeId radioButton,
                                                             const UI::UIRadioButtonPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setRadioButtonPaint");
    }
    return m_state->setRadioButtonPaint(m_epoch, m_phase, m_updater, radioButton, paint);
}

Core::Result<UI::UIRadioButtonPaint> PrimaryWindowUITreeUpdater::radioButtonPaint(UI::UINodeId radioButton) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIRadioButtonPaint>("PrimaryWindowUITreeUpdater::radioButtonPaint");
    }
    return m_state->radioButtonPaint(m_epoch, m_phase, m_updater, radioButton);
}

Core::Status PrimaryWindowUITreeUpdater::setRadioButtonAction(UI::UINodeId radioButton,
                                                              UI::UIButtonActionCallback callback)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setRadioButtonAction");
    }
    return m_state->setRadioButtonAction(m_epoch, m_phase, m_updater, radioButton, std::move(callback));
}

Core::Status PrimaryWindowUITreeUpdater::clearRadioButtonAction(UI::UINodeId radioButton)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearRadioButtonAction");
    }
    return m_state->clearRadioButtonAction(m_epoch, m_phase, m_updater, radioButton);
}

Core::Status PrimaryWindowUITreeUpdater::setRadioButtonSelected(UI::UINodeId radioButton, bool selected)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setRadioButtonSelected");
    }
    return m_state->setRadioButtonSelected(m_epoch, m_phase, m_updater, radioButton, selected);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isRadioButtonSelected(UI::UINodeId radioButton) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isRadioButtonSelected");
    }
    return m_state->isRadioButtonSelected(m_epoch, m_phase, m_updater, radioButton);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isRadioButtonPressed(UI::UINodeId radioButton) const
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
