#include "IosCompositionSession.hpp"

#include <algorithm>
#include <cstring>

namespace Tina::Platform::Detail {

std::string_view IosCompositionSession::preeditUtf8() const noexcept
{
    if (!active_)
    {
        return {};
    }
    return std::string_view{preeditBytes_.data(), preeditSize_};
}

void IosCompositionSession::storePreedit(const IosCompositionEvent& event) noexcept
{
    const auto byteCount = (std::min)(static_cast<usize>(event.byteCount), preeditBytes_.size());
    if (byteCount != 0)
    {
        std::memcpy(preeditBytes_.data(), event.utf8.data(), byteCount);
    }
    preeditSize_ = byteCount;
    cursorCodepoint_ = event.cursorCodepoint;
    active_ = true;
}

void IosCompositionSession::clear() noexcept
{
    preeditSize_ = 0;
    cursorCodepoint_ = 0;
    active_ = false;
}

IosCompositionOutcome IosCompositionSession::apply(const IosCompositionEvent& event) noexcept
{
    switch (event.action)
    {
    case IosCompositionAction::SetMarkedText:
    {
        if (event.byteCount == 0)
        {
            // An emptied marked region. UITextInput has no cancel call, so deleting the last
            // character of a preedit is the same call as setting it -- with an empty string.
            // Cancelled rather than Ended because nothing was produced: the user backed out.
            //
            // Silent when nothing was in flight. The input system clears the region routinely as it
            // attaches and detaches, and announcing the end of a composition that never started is
            // noise every consumer would have to filter.
            if (!active_)
            {
                return IosCompositionOutcome{};
            }
            clear();
            return IosCompositionOutcome{.stage = TextCompositionStage::Cancelled};
        }

        // Started only for the first text of a pass. The distinction is not in the UITextInput call
        // at all, which is precisely why the session has to remember.
        const TextCompositionStage stage =
            active_ ? TextCompositionStage::Updated : TextCompositionStage::Started;
        storePreedit(event);
        return IosCompositionOutcome{
            .stage = stage,
            .preeditUtf8 = preeditUtf8(),
            .cursorCodepoint = cursorCodepoint_,
        };
    }

    case IosCompositionAction::Unmark:
        // unmarkText abandons the region without producing text, so Cancelled -- same reasoning as an
        // emptied preedit. Ended would claim the composition resolved into something.
        //
        // UIKit also calls unmarkText when a view resigns first responder, which is why the
        // not-active case must stay silent rather than emitting a stage on every focus change.
        if (!active_)
        {
            return IosCompositionOutcome{};
        }
        clear();
        return IosCompositionOutcome{.stage = TextCompositionStage::Cancelled};

    case IosCompositionAction::Commit:
    {
        const std::string_view committed{event.utf8.data(), event.byteCount};
        if (!active_)
        {
            // A commit with no marked text in flight: a Latin software keyboard, a paste, dictation,
            // or an autocorrect acceptance. Text only, which is exactly the pre-preedit behaviour.
            return IosCompositionOutcome{.committedUtf8 = committed};
        }
        // Ended is carried alongside the text rather than published by a later call, so the stage
        // always precedes the text it resolved into. Reversing them still "works" -- UI's
        // routeTextInput clears the composition itself -- but the frame would then read as "text
        // appeared, then the composition ended", which is wrong for anything rebuilding input-method
        // state from the stage sequence. Same order the IMM32 and Android hosts publish.
        clear();
        return IosCompositionOutcome{
            .stage = TextCompositionStage::Ended,
            .committedUtf8 = committed,
        };
    }
    }
    return IosCompositionOutcome{};
}

std::optional<IosCompositionOutcome> IosCompositionSession::cancel() noexcept
{
    if (!active_)
    {
        return std::nullopt;
    }
    clear();
    return IosCompositionOutcome{.stage = TextCompositionStage::Cancelled};
}

} // namespace Tina::Platform::Detail
