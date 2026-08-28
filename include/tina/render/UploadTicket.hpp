#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderErrors.hpp>

#include <cstddef>
#include <limits>
#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Render {

// Backend-neutral CPU-side staging ownership for a queued GPU upload (ADR 0016).
// This Null ledger owns the staging copy until an explicit retire and may advance
// Pending → Ready immediately.
//
// It is deliberately NOT a GPU completion mechanism. ADR 0016 originally cast the
// ticket as the upload itself, to be driven toward Ready by a backend fence, but
// RENDER-FENCE settled on a separate proof: a 1x1 BLIT_DST | READ_BACK marker whose
// readTexture() ready frame completes the retirement timeline. So the ticket kept
// only the half it actually implements -- who owns the staging bytes and when they
// are freed -- and GPU resource retirement does not reuse it (docs/rendering.md).
//
// That leaves no failure state to report: submit() copies the bytes and either
// returns a ticket or fails immediately, so once a ticket exists nothing downstream
// can fail. There is no Failed enumerator for that reason. A future ledger that does
// wait on a real fence would need one, and would have to define who publishes it.
enum class UploadTicketState : Core::u8 {
    Invalid = 0,
    Pending = 1,
    Ready = 2,
    Retired = 3,
};

struct UploadTicketId final {
    Core::u32 index = (std::numeric_limits<Core::u32>::max)();
    Core::u32 generation = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return index != (std::numeric_limits<Core::u32>::max)() && generation != 0;
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return hasValue();
    }
    [[nodiscard]] friend constexpr bool operator==(const UploadTicketId&, const UploadTicketId&) = default;
};

struct UploadSubmitParams final {
    std::span<const std::byte> bytes{};
    // Reserved for future GPU resource binding; ignored by Null ledger.
    Core::u64 userTag = 0;
};

struct UploadLedgerConfig final {
    Core::usize capacity = 0;
    std::pmr::memory_resource* memoryResource = nullptr;
};

// Owner-thread CPU-side ledger for staging ownership. Not thread-safe.
// Null path: submit copies staging, poll() promotes Pending→Ready, retire frees staging.
class NullUploadLedger final {
  public:
    NullUploadLedger() = delete;
    ~NullUploadLedger() noexcept;

    NullUploadLedger(const NullUploadLedger&) = delete;
    NullUploadLedger& operator=(const NullUploadLedger&) = delete;
    NullUploadLedger(NullUploadLedger&&) noexcept;
    NullUploadLedger& operator=(NullUploadLedger&&) = delete;

    [[nodiscard]] static Core::Result<NullUploadLedger> Create(UploadLedgerConfig config);

    [[nodiscard]] Core::usize capacity() const noexcept;
    [[nodiscard]] Core::u32 liveCount() const noexcept;
    [[nodiscard]] Core::u32 pendingCount() const noexcept;
    [[nodiscard]] Core::u32 readyCount() const noexcept;

    // Copies bytes into owned staging and returns a Pending ticket.
    [[nodiscard]] Core::Result<UploadTicketId> submit(UploadSubmitParams params);

    // Null backend: Pending → Ready immediately (simulates a completed upload).
    // A backend that waits on a real fence would drive this transition instead.
    [[nodiscard]] Core::Status poll(UploadTicketId ticket) noexcept;

    [[nodiscard]] UploadTicketState state(UploadTicketId ticket) const noexcept;
    [[nodiscard]] std::span<const std::byte> staging(UploadTicketId ticket) const noexcept;
    [[nodiscard]] Core::u64 userTag(UploadTicketId ticket) const noexcept;

    // Frees staging. Allowed only from Ready. Invalidates the ticket.
    [[nodiscard]] Core::Status retire(UploadTicketId ticket) noexcept;

  private:
    struct Slot final {
        Core::u32 generation = 1;
        UploadTicketState state = UploadTicketState::Retired;
        Core::u64 userTag = 0;
        std::pmr::vector<std::byte> staging{};
        bool occupied = false;
    };

    explicit NullUploadLedger(std::pmr::memory_resource* memoryResource, std::pmr::vector<Slot> slots) noexcept;

    [[nodiscard]] Slot* findSlot(UploadTicketId ticket) noexcept;
    [[nodiscard]] const Slot* findSlot(UploadTicketId ticket) const noexcept;

    std::pmr::memory_resource* m_memoryResource = nullptr;
    std::pmr::vector<Slot> m_slots;
    Core::u32 m_live = 0;
    Core::u32 m_pending = 0;
    Core::u32 m_ready = 0;
};

} // namespace Tina::Render
