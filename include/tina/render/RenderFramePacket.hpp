#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFrame.hpp>

#include <array>
#include <limits>

namespace Tina::Render {

// Runtime-private owning frame packet (RUNTIME-002 first slice).
// Holds type-erased FramePins for the duration of submit/present (or abandon).
// Does not replace RenderFrame borrow views; it owns lifetime side-channel pins.
class RenderFramePacket final : public FramePinSink {
public:
    static constexpr Core::u32 MaxPins = 32;

    enum class State : Core::u8 {
        Idle = 0,
        Building = 1,
        Submitted = 2,
        Completed = 3,
        Abandoned = 4,
    };

    RenderFramePacket() = default;
    RenderFramePacket(const RenderFramePacket&) = delete;
    RenderFramePacket& operator=(const RenderFramePacket&) = delete;

    ~RenderFramePacket() noexcept override { (void)abandon(); }

    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] Core::u64 frameIndex() const noexcept { return m_frameIndex; }
    [[nodiscard]] SubmissionTicket ticket() const noexcept { return m_ticket; }
    [[nodiscard]] Core::u32 pinCount() const noexcept override { return m_pinCount; }

    // Begin a new frame. Abandons any previous incomplete packet.
    [[nodiscard]] Core::Status beginFrame(Core::u64 frameIndex) noexcept
    {
        if (m_state == State::Building || m_state == State::Submitted)
        {
            (void)abandon();
        }
        m_frameIndex = frameIndex;
        m_pinCount = 0;
        m_ticket = {};
        m_state = State::Building;
        return Core::success();
    }

    [[nodiscard]] FramePinSink& pinSink() noexcept { return *this; }

    [[nodiscard]] Core::Status add(FramePinKind kind, FramePin pin) override
    {
        if (m_state != State::Building)
        {
            return Core::failure(RenderErrorCode::InvalidFramePin,
                                 "FramePinSink only accepts pins while the packet is Building");
        }
        if (kind == FramePinKind::Invalid || !pin.hasValue())
        {
            return Core::failure(RenderErrorCode::InvalidFramePin, "FramePin must have a valid kind");
        }
        if (m_pinCount >= MaxPins)
        {
            return Core::failure(RenderErrorCode::FramePinCapacityExceeded,
                                 "RenderFramePacket pin capacity exhausted");
        }
        m_pins[m_pinCount] = std::move(pin);
        ++m_pinCount;
        return Core::success();
    }

    // Attach a Submitted submission ticket (Null ledger open). Packet must be Building.
    [[nodiscard]] Core::Status attachSubmission(SubmissionTicket ticket) noexcept
    {
        if (m_state != State::Building)
        {
            return Core::failure(RenderErrorCode::InvalidSubmissionTicket,
                                 "attachSubmission requires Building packet");
        }
        if (!ticket.hasValue())
        {
            return Core::failure(RenderErrorCode::InvalidSubmissionTicket, "SubmissionTicket is not open");
        }
        m_ticket = ticket;
        m_state = State::Submitted;
        return Core::success();
    }

    // Complete after present (or Null sync completion). Releases all pins and closes ticket via ledger.
    // Ledger may be Null (present-sync) or FrameDeferred (Host completes after lag).
    [[nodiscard]] Core::Status complete(ISubmissionCompletionLedger& ledger) noexcept
    {
        if (m_state == State::Idle || m_state == State::Completed || m_state == State::Abandoned)
        {
            return Core::success();
        }
        if (m_state == State::Submitted)
        {
            if (auto status = ledger.complete(m_ticket); !status)
            {
                return status;
            }
        }
        releasePins();
        m_state = State::Completed;
        return Core::success();
    }

    // FrameDeferred handoff: move pins + open ticket out; packet becomes Completed without
    // releasing pins or closing the ledger ticket. Caller must later complete the ticket
    // and release pins (next present lag or shutdown).
    struct DeferredHandoff final {
        SubmissionTicket ticket{};
        std::array<FramePin, MaxPins> pins{};
        Core::u32 pinCount = 0;
        Core::u64 frameIndex = 0;
        Core::u64 presentFrameToken = 0;
    };

    [[nodiscard]] Core::Result<DeferredHandoff> handOffDeferred(Core::u64 presentFrameToken) noexcept
    {
        if (m_state != State::Submitted)
        {
            return Core::failure(RenderErrorCode::InvalidSubmissionTicket,
                                 "handOffDeferred requires Submitted packet");
        }
        DeferredHandoff handoff{};
        handoff.ticket = m_ticket;
        handoff.pinCount = m_pinCount;
        handoff.frameIndex = m_frameIndex;
        handoff.presentFrameToken = presentFrameToken;
        for (Core::u32 i = 0; i < m_pinCount; ++i)
        {
            handoff.pins[i] = std::move(m_pins[i]);
        }
        m_pinCount = 0;
        m_ticket = {};
        m_state = State::Completed;
        return handoff;
    }

    // Release pins from a previous handOffDeferred and close the ledger ticket.
    [[nodiscard]] static Core::Status completeDeferred(ISubmissionCompletionLedger& ledger,
                                                       DeferredHandoff& handoff) noexcept
    {
        if (handoff.ticket.hasValue())
        {
            if (auto status = ledger.complete(handoff.ticket); !status)
            {
                return status;
            }
        }
        for (Core::u32 i = 0; i < handoff.pinCount; ++i)
        {
            handoff.pins[i].release();
        }
        handoff.pinCount = 0;
        handoff.ticket = {};
        handoff.presentFrameToken = 0;
        return Core::success();
    }

    [[nodiscard]] static Core::Status abandonDeferred(ISubmissionCompletionLedger* ledger,
                                                      DeferredHandoff& handoff) noexcept
    {
        if (handoff.ticket.hasValue() && ledger != nullptr)
        {
            (void)ledger->abandon(handoff.ticket);
        }
        for (Core::u32 i = 0; i < handoff.pinCount; ++i)
        {
            handoff.pins[i].release();
        }
        handoff.pinCount = 0;
        handoff.ticket = {};
        handoff.presentFrameToken = 0;
        return Core::success();
    }

    // Complete a Building packet that never submitted (skipped suspended surface).
    [[nodiscard]] Core::Status completeSkipped() noexcept
    {
        if (m_state != State::Building)
        {
            if (m_state == State::Completed || m_state == State::Abandoned || m_state == State::Idle)
            {
                return Core::success();
            }
            return Core::failure(RenderErrorCode::InvalidFramePin,
                                 "completeSkipped requires Building packet");
        }
        releasePins();
        m_state = State::Completed;
        return Core::success();
    }

    // Failure path: abandon in-flight ticket (if any) and release pins.
    [[nodiscard]] Core::Status abandon(ISubmissionCompletionLedger* ledger = nullptr) noexcept
    {
        if (m_state == State::Idle || m_state == State::Completed || m_state == State::Abandoned)
        {
            return Core::success();
        }
        if (m_state == State::Submitted && ledger != nullptr)
        {
            (void)ledger->abandon(m_ticket);
        }
        releasePins();
        m_state = State::Abandoned;
        return Core::success();
    }

private:
    void releasePins() noexcept
    {
        for (Core::u32 i = 0; i < m_pinCount; ++i)
        {
            m_pins[i].release();
        }
        m_pinCount = 0;
        m_ticket = {};
    }

    State m_state = State::Idle;
    Core::u64 m_frameIndex = 0;
    SubmissionTicket m_ticket{};
    std::array<FramePin, MaxPins> m_pins{};
    Core::u32 m_pinCount = 0;
};

} // namespace Tina::Render
