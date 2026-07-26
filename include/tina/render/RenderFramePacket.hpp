#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/FrameResource.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFrame.hpp>

#include <array>
#include <atomic>
#include <limits>
#include <optional>
#include <utility>

namespace Tina::Render {

namespace Detail {

[[nodiscard]] inline Core::u64 acquireFrameResourcePacketOwner() noexcept
{
    static std::atomic<Core::u64> nextOwner{1};
    Core::u64 current = nextOwner.load(std::memory_order_relaxed);
    while (current != 0)
    {
        const Core::u64 next = current == (std::numeric_limits<Core::u64>::max)()
            ? 0
            : current + 1;
        if (nextOwner.compare_exchange_weak(
                current,
                next,
                std::memory_order_relaxed,
                std::memory_order_relaxed))
        {
            return current;
        }
    }
    return 0;
}

} // namespace Detail

// Runtime-private owning frame packet (RUNTIME-002 first slice).
// Holds type-erased FramePins for the duration of submit/present (or abandon).
// Does not replace RenderFrame borrow views; it owns lifetime side-channel pins.
class RenderFramePacket final : public FramePinSink, public FrameResourceSink {
public:
    static constexpr Core::u32 MaxPins = 32;
    static constexpr Core::u32 MaxResources = 64;

    enum class State : Core::u8 {
        Idle = 0,
        Building = 1,
        Submitted = 2,
        Completed = 3,
        Abandoned = 4,
    };

    RenderFramePacket() noexcept
        : m_resourcePacketOwner(Detail::acquireFrameResourcePacketOwner())
    {
    }
    RenderFramePacket(const RenderFramePacket&) = delete;
    RenderFramePacket& operator=(const RenderFramePacket&) = delete;

    ~RenderFramePacket() noexcept override { (void)abandon(); }

    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] Core::u64 frameIndex() const noexcept { return m_frameIndex; }
    [[nodiscard]] Core::u64 submissionIndex() const noexcept
    {
        return m_ticket.has_value() ? m_ticket->submissionIndex() : 0;
    }
    [[nodiscard]] Core::u32 pinCount() const noexcept override { return m_pinCount; }
    [[nodiscard]] Core::u32 resourceCount() const noexcept override { return m_resourceCount; }

    // Begin a new frame. Abandons any previous incomplete packet.
    [[nodiscard]] Core::Status beginFrame(Core::u64 frameIndex) noexcept
    {
        if (m_resourcePacketOwner == 0
            || m_frameGeneration == (std::numeric_limits<Core::u64>::max)())
        {
            return Core::failure(RenderErrorCode::FrameResourceIdentityExhausted,
                                 "RenderFramePacket resource identity space is exhausted");
        }
        if (m_state == State::Building || m_state == State::Submitted)
        {
            if (auto status = abandon(); !status)
            {
                return status;
            }
        }
        m_frameIndex = frameIndex;
        m_pinCount = 0;
        m_resourceCount = 0;
        m_ticket.reset();
        m_ledger = nullptr;
        ++m_frameGeneration;
        m_liveFrameGeneration = m_frameGeneration;
        m_state = State::Building;
        return Core::success();
    }

    [[nodiscard]] FramePinSink& pinSink() noexcept { return *this; }
    [[nodiscard]] FrameResourceSink& resourceSink() noexcept { return *this; }

    [[nodiscard]] FrameResourceTableView resourceTableView() const noexcept
    {
        if (m_liveFrameGeneration == 0)
        {
            return {};
        }
        return FrameResourceTableView{
            m_resourceDescriptors.data(),
            m_resourceCount,
            &m_liveFrameGeneration,
            m_resourcePacketOwner,
            m_liveFrameGeneration,
        };
    }

    [[nodiscard]] Core::Status add(FramePinKind kind, FramePin&& pin) override
    {
        if (m_state != State::Building)
        {
            return Core::failure(RenderErrorCode::InvalidFramePin,
                                 "FramePinSink only accepts pins while the packet is Building");
        }
        if (kind == FramePinKind::Invalid || !pin.hasValue() || pin.kind() != kind)
        {
            return Core::failure(
                RenderErrorCode::InvalidFramePin,
                "FramePin must have a valid kind matching the sink registration kind");
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

    [[nodiscard]] Core::Result<FrameResourceRef> intern(
        FrameResourceDescriptor descriptor,
        FramePin&& pin) override
    {
        if (m_state != State::Building)
        {
            return Core::failure(
                RenderErrorCode::InvalidFrameResource,
                "FrameResourceSink only accepts resources while the packet is Building");
        }
        if (descriptor.kind != FrameResourceKind::Sprite2DTexture
            || descriptor.deviceBindingKey == 0)
        {
            return Core::failure(
                RenderErrorCode::InvalidFrameResource,
                "FrameResourceDescriptor requires a valid kind and non-zero device binding key");
        }
        if (!pin.hasValue())
        {
            return Core::failure(RenderErrorCode::InvalidFrameResource,
                                 "FrameResource requires an owning pin");
        }

        for (Core::u32 index = 0; index < m_resourceCount; ++index)
        {
            if (m_resourceDescriptors[index] == descriptor)
            {
                pin.release();
                return FrameResourceRef::createForPacket(
                    m_resourcePacketOwner,
                    m_liveFrameGeneration,
                    index);
            }
        }

        if (m_resourceCount >= MaxResources)
        {
            return Core::failure(RenderErrorCode::FrameResourceCapacityExceeded,
                                 "RenderFramePacket resource capacity exhausted");
        }

        const Core::u32 index = m_resourceCount;
        m_resourceDescriptors[index] = descriptor;
        m_resourcePins[index] = std::move(pin);
        ++m_resourceCount;
        return FrameResourceRef::createForPacket(
            m_resourcePacketOwner,
            m_liveFrameGeneration,
            index);
    }

    // The ledger must outlive this packet while the ticket remains open.
    [[nodiscard]] Core::Status attachSubmission(ISubmissionCompletionLedger& ledger,
                                                SubmissionTicket&& ticket) noexcept
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
        if (!ticket.belongsTo(ledger))
        {
            return Core::failure(RenderErrorCode::InvalidSubmissionTicket,
                                 "SubmissionTicket belongs to a different completion ledger");
        }
        m_ticket.emplace(std::move(ticket));
        m_ledger = &ledger;
        m_state = State::Submitted;
        return Core::success();
    }

    // Complete after present returns. Releases all CPU lifetime pins and closes the
    // submission ticket. This is not a GPU-resource retirement signal.
    [[nodiscard]] Core::Status complete() noexcept
    {
        if (m_state == State::Idle || m_state == State::Completed || m_state == State::Abandoned)
        {
            return Core::success();
        }
        if (m_state == State::Submitted)
        {
            if (m_ledger == nullptr || !m_ticket.has_value())
            {
                return Core::failure(RenderErrorCode::InvalidSubmissionTicket,
                                     "Submitted packet has no completion ledger or ticket");
            }
            if (auto status = m_ledger->complete(*m_ticket); !status)
            {
                return status;
            }
        }
        releaseFrameOwnership();
        m_state = State::Completed;
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
        releaseFrameOwnership();
        m_state = State::Completed;
        return Core::success();
    }

    // Failure path: abandon in-flight ticket (if any) and release pins.
    [[nodiscard]] Core::Status abandon() noexcept
    {
        if (m_state == State::Idle || m_state == State::Completed || m_state == State::Abandoned)
        {
            return Core::success();
        }
        if (m_state == State::Submitted)
        {
            if (m_ledger == nullptr || !m_ticket.has_value())
            {
                return Core::failure(RenderErrorCode::InvalidSubmissionTicket,
                                     "Submitted packet has no completion ledger or ticket");
            }
            if (auto status = m_ledger->abandon(*m_ticket); !status)
            {
                return status;
            }
        }
        releaseFrameOwnership();
        m_state = State::Abandoned;
        return Core::success();
    }

private:
    void releaseFrameOwnership() noexcept
    {
        for (Core::u32 i = 0; i < m_pinCount; ++i)
        {
            m_pins[i].release();
        }
        m_pinCount = 0;
        for (Core::u32 i = 0; i < m_resourceCount; ++i)
        {
            m_resourcePins[i].release();
        }
        m_resourceCount = 0;
        m_liveFrameGeneration = 0;
        m_ticket.reset();
        m_ledger = nullptr;
    }

    State m_state = State::Idle;
    Core::u64 m_frameIndex = 0;
    std::optional<SubmissionTicket> m_ticket{};
    ISubmissionCompletionLedger* m_ledger = nullptr;
    std::array<FramePin, MaxPins> m_pins{};
    Core::u32 m_pinCount = 0;
    const Core::u64 m_resourcePacketOwner = 0;
    Core::u64 m_frameGeneration = 0;
    Core::u64 m_liveFrameGeneration = 0;
    std::array<FrameResourceDescriptor, MaxResources> m_resourceDescriptors{};
    std::array<FramePin, MaxResources> m_resourcePins{};
    Core::u32 m_resourceCount = 0;
};

} // namespace Tina::Render
