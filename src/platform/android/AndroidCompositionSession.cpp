#include "AndroidCompositionSession.hpp"

#include <algorithm>
#include <cstring>

namespace Tina::Platform::Detail {

std::string_view AndroidCompositionSession::preeditUtf8() const noexcept
{
    if (!active_)
    {
        return {};
    }
    return std::string_view{preeditBytes_.data(), preeditSize_};
}

void AndroidCompositionSession::storePreedit(const AndroidCompositionEvent& event) noexcept
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

void AndroidCompositionSession::clear() noexcept
{
    preeditSize_ = 0;
    cursorCodepoint_ = 0;
    active_ = false;
}

AndroidCompositionOutcome AndroidCompositionSession::apply(const AndroidCompositionEvent& event) noexcept
{
    switch (event.action)
    {
    case AndroidCompositionAction::SetText:
    {
        if (event.byteCount == 0)
        {
            // An emptied composing region. Android has no cancel call, so deleting the last character
            // of a preedit is the same call as setting it -- with an empty string. Cancelled rather
            // than Ended because nothing was produced: the user backed out.
            //
            // Silent when nothing was in flight. IMEs clear the region routinely as they attach and
            // detach, and announcing the end of a composition that never started is noise every
            // consumer would have to filter.
            if (!active_)
            {
                return AndroidCompositionOutcome{};
            }
            clear();
            return AndroidCompositionOutcome{.stage = TextCompositionStage::Cancelled};
        }

        // Started only for the first text of a pass. The distinction is not in the Android call at
        // all, which is precisely why the session has to remember.
        const TextCompositionStage stage =
            active_ ? TextCompositionStage::Updated : TextCompositionStage::Started;
        storePreedit(event);
        return AndroidCompositionOutcome{
            .stage = stage,
            .preeditUtf8 = preeditUtf8(),
            .cursorCodepoint = cursorCodepoint_,
        };
    }

    case AndroidCompositionAction::Finish:
        // finishComposingText abandons the region without producing text, so Cancelled -- same
        // reasoning as an emptied preedit. Ended would claim the composition resolved into something.
        if (!active_)
        {
            return AndroidCompositionOutcome{};
        }
        clear();
        return AndroidCompositionOutcome{.stage = TextCompositionStage::Cancelled};

    case AndroidCompositionAction::Commit:
    {
        const std::string_view committed{event.utf8.data(), event.byteCount};
        if (!active_)
        {
            // A commit with no composition in flight: an ASCII soft keyboard, a paste, or an
            // autocomplete pick. Text only, which is exactly the pre-preedit behaviour.
            return AndroidCompositionOutcome{.committedUtf8 = committed};
        }
        // Ended is carried alongside the text rather than published by a later call, so the stage
        // always precedes the text it resolved into. Reversing them still "works" -- UI's
        // routeTextInput clears the composition itself -- but the frame would then read as "text
        // appeared, then the composition ended", which is wrong for anything rebuilding IME state
        // from the stage sequence. Same order the IMM32 host publishes.
        clear();
        return AndroidCompositionOutcome{
            .stage = TextCompositionStage::Ended,
            .committedUtf8 = committed,
        };
    }
    }
    return AndroidCompositionOutcome{};
}

std::optional<AndroidCompositionOutcome> AndroidCompositionSession::cancel() noexcept
{
    if (!active_)
    {
        return std::nullopt;
    }
    clear();
    return AndroidCompositionOutcome{.stage = TextCompositionStage::Cancelled};
}

} // namespace Tina::Platform::Detail
