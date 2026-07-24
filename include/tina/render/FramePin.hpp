#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderErrors.hpp>

#include <utility>

namespace Tina::Render {

// Type-erased frame pin kinds registered on a RenderFramePacket (ADR 0016 / RUNTIME-002).
enum class FramePinKind : Core::u8 {
    Invalid = 0,
    Surface = 1,
    GlyphAtlas = 2,
    AssetLease = 3,
    Custom = 4,
};

// Owning, movable pin. release() runs at most once (complete or abandon).
class FramePin final {
public:
    using ReleaseFn = void (*)(void* userData) noexcept;

    FramePin() noexcept = default;

    FramePin(FramePinKind kind, Core::u64 userTag, void* userData, ReleaseFn release) noexcept
        : m_kind(kind), m_userTag(userTag), m_userData(userData), m_release(release)
    {
    }

    FramePin(const FramePin&) = delete;
    FramePin& operator=(const FramePin&) = delete;

    FramePin(FramePin&& other) noexcept
        : m_kind(other.m_kind),
          m_userTag(other.m_userTag),
          m_userData(other.m_userData),
          m_release(other.m_release)
    {
        other.clearWithoutRelease();
    }

    FramePin& operator=(FramePin&& other) noexcept
    {
        if (this != &other)
        {
            release();
            m_kind = other.m_kind;
            m_userTag = other.m_userTag;
            m_userData = other.m_userData;
            m_release = other.m_release;
            other.clearWithoutRelease();
        }
        return *this;
    }

    ~FramePin() noexcept { release(); }

    [[nodiscard]] FramePinKind kind() const noexcept { return m_kind; }
    [[nodiscard]] Core::u64 userTag() const noexcept { return m_userTag; }
    [[nodiscard]] bool hasValue() const noexcept { return m_kind != FramePinKind::Invalid; }

    void release() noexcept
    {
        if (m_release != nullptr)
        {
            m_release(m_userData);
        }
        clearWithoutRelease();
    }

private:
    void clearWithoutRelease() noexcept
    {
        m_kind = FramePinKind::Invalid;
        m_userTag = 0;
        m_userData = nullptr;
        m_release = nullptr;
    }

    FramePinKind m_kind = FramePinKind::Invalid;
    Core::u64 m_userTag = 0;
    void* m_userData = nullptr;
    ReleaseFn m_release = nullptr;
};

// Narrow SPI: Asset/UI may register pins while building a frame; they must not
// hold the sink pointer after the packet is completed/abandoned.
class FramePinSink {
public:
    virtual ~FramePinSink() noexcept = default;
    [[nodiscard]] virtual Core::Status add(FramePinKind kind, FramePin pin) = 0;
    [[nodiscard]] virtual Core::u32 pinCount() const noexcept = 0;
};

// Tracks Submitted frame indices until complete/abandon (ADR 0016 / RUNTIME-002).
// Host holds an ISubmissionCompletionLedger; Null completes at present-return.
// Future Bgfx* ledger will complete from GPU fence without changing packet/Host shape.
struct SubmissionTicket final {
    Core::u64 submissionIndex = 0;
    bool open = false;

    [[nodiscard]] bool hasValue() const noexcept { return open; }
};

class ISubmissionCompletionLedger {
public:
    virtual ~ISubmissionCompletionLedger() noexcept = default;

    [[nodiscard]] virtual Core::Result<SubmissionTicket> beginSubmitted(Core::u64 submissionIndex) = 0;
    [[nodiscard]] virtual Core::Status complete(SubmissionTicket& ticket) noexcept = 0;
    [[nodiscard]] virtual Core::Status abandon(SubmissionTicket& ticket) noexcept = 0;
    [[nodiscard]] virtual Core::u32 inflightCount() const noexcept = 0;
    [[nodiscard]] virtual bool allClear() const noexcept = 0;
};

// Sync Null ledger: present-return == complete. Not a GPU fence.
class NullSubmissionCompletionLedger final : public ISubmissionCompletionLedger {
public:
    static constexpr Core::usize DefaultCapacity = 64;

    explicit NullSubmissionCompletionLedger(Core::usize capacity = DefaultCapacity) noexcept
        : m_capacity(capacity == 0 ? DefaultCapacity : capacity)
    {
    }

    [[nodiscard]] Core::usize capacity() const noexcept { return m_capacity; }
    [[nodiscard]] Core::u32 inflightCount() const noexcept override { return m_inflight; }
    [[nodiscard]] Core::u64 lastCompletedSubmission() const noexcept { return m_lastCompleted; }
    [[nodiscard]] bool allClear() const noexcept override { return m_inflight == 0; }

    [[nodiscard]] Core::Result<SubmissionTicket> beginSubmitted(Core::u64 submissionIndex) override
    {
        if (m_inflight >= static_cast<Core::u32>(m_capacity))
        {
            return Core::failure(RenderErrorCode::SubmissionCompletionLedgerFull,
                                 "NullSubmissionCompletionLedger in-flight capacity exhausted");
        }
        // submissionIndex may be 0 on the first engine frame (Null/bgfx paths).
        ++m_inflight;
        ++m_opened;
        return SubmissionTicket{.submissionIndex = submissionIndex, .open = true};
    }

    [[nodiscard]] Core::Status complete(SubmissionTicket& ticket) noexcept override
    {
        if (!ticket.open)
        {
            return Core::success();
        }
        if (m_inflight == 0)
        {
            ticket.open = false;
            return Core::failure(RenderErrorCode::InvalidSubmissionTicket,
                                 "NullSubmissionCompletionLedger has no in-flight submissions");
        }
        --m_inflight;
        m_lastCompleted = ticket.submissionIndex;
        ++m_completed;
        ticket.open = false;
        return Core::success();
    }

    [[nodiscard]] Core::Status abandon(SubmissionTicket& ticket) noexcept override
    {
        if (!ticket.open)
        {
            return Core::success();
        }
        if (m_inflight == 0)
        {
            ticket.open = false;
            return Core::failure(RenderErrorCode::InvalidSubmissionTicket,
                                 "NullSubmissionCompletionLedger has no in-flight submissions");
        }
        --m_inflight;
        ++m_abandoned;
        ticket.open = false;
        return Core::success();
    }

    [[nodiscard]] Core::u64 openedCount() const noexcept { return m_opened; }
    [[nodiscard]] Core::u64 completedCount() const noexcept { return m_completed; }
    [[nodiscard]] Core::u64 abandonedCount() const noexcept { return m_abandoned; }

    void resetCountersForTest() noexcept
    {
        m_inflight = 0;
        m_opened = 0;
        m_completed = 0;
        m_abandoned = 0;
        m_lastCompleted = 0;
    }

private:
    Core::usize m_capacity = DefaultCapacity;
    Core::u32 m_inflight = 0;
    Core::u64 m_opened = 0;
    Core::u64 m_completed = 0;
    Core::u64 m_abandoned = 0;
    Core::u64 m_lastCompleted = 0;
};

} // namespace Tina::Render
