#include <tina/render/UploadTicket.hpp>

#include <limits>
#include <new>
#include <utility>

namespace Tina::Render {

NullUploadLedger::NullUploadLedger(std::pmr::memory_resource* memoryResource, std::pmr::vector<Slot> slots) noexcept
    : m_memoryResource(memoryResource), m_slots(std::move(slots))
{
}

NullUploadLedger::~NullUploadLedger() noexcept = default;

NullUploadLedger::NullUploadLedger(NullUploadLedger&&) noexcept = default;

Core::Result<NullUploadLedger> NullUploadLedger::Create(UploadLedgerConfig config)
{
    if (config.memoryResource == nullptr || config.capacity == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "upload ledger requires capacity and memory resource");
    }
    if (config.capacity > (std::numeric_limits<Core::u32>::max)())
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded, "upload ledger capacity too large");
    }
    try
    {
        std::pmr::vector<Slot> slots{config.memoryResource};
        slots.resize(config.capacity);
        for (auto& slot : slots)
        {
            slot.staging = std::pmr::vector<std::byte>{config.memoryResource};
            slot.generation = 1;
            slot.state = UploadTicketState::Retired;
            slot.occupied = false;
        }
        return NullUploadLedger(config.memoryResource, std::move(slots));
    } catch (const std::bad_alloc&)
    {
        return Core::failure(RenderErrorCode::DisplayListStorageAllocationFailed, "upload ledger allocation failed");
    }
}

Core::usize NullUploadLedger::capacity() const noexcept
{
    return m_slots.size();
}

Core::u32 NullUploadLedger::liveCount() const noexcept
{
    return m_live;
}

Core::u32 NullUploadLedger::pendingCount() const noexcept
{
    return m_pending;
}

Core::u32 NullUploadLedger::readyCount() const noexcept
{
    return m_ready;
}

Core::Result<UploadTicketId> NullUploadLedger::submit(UploadSubmitParams params)
{
    if (params.bytes.empty())
    {
        return Core::failure(RenderErrorCode::InvalidDrawCommand, "upload submit requires non-empty staging bytes");
    }
    for (Core::u32 index = 0; index < static_cast<Core::u32>(m_slots.size()); ++index)
    {
        auto& slot = m_slots[index];
        if (slot.occupied)
        {
            continue;
        }
        try
        {
            slot.staging.assign(params.bytes.begin(), params.bytes.end());
        } catch (const std::bad_alloc&)
        {
            return Core::failure(RenderErrorCode::DisplayListStorageAllocationFailed, "upload staging allocation failed");
        }
        slot.occupied = true;
        slot.state = UploadTicketState::Pending;
        slot.userTag = params.userTag;
        if (slot.generation == 0)
        {
            slot.generation = 1;
        }
        ++m_live;
        ++m_pending;
        return UploadTicketId{.index = index, .generation = slot.generation};
    }
    return Core::failure(RenderErrorCode::UploadLedgerFull, "upload ledger capacity exceeded");
}

Core::Status NullUploadLedger::poll(UploadTicketId ticket) noexcept
{
    auto* slot = findSlot(ticket);
    if (slot == nullptr)
    {
        return Core::failure(RenderErrorCode::UploadTicketInvalid, "upload ticket is invalid or stale");
    }
    if (slot->state == UploadTicketState::Pending)
    {
        slot->state = UploadTicketState::Ready;
        if (m_pending > 0U)
        {
            --m_pending;
        }
        ++m_ready;
    }
    return Core::success();
}

UploadTicketState NullUploadLedger::state(UploadTicketId ticket) const noexcept
{
    const auto* slot = findSlot(ticket);
    if (slot == nullptr)
    {
        return UploadTicketState::Invalid;
    }
    return slot->state;
}

std::span<const std::byte> NullUploadLedger::staging(UploadTicketId ticket) const noexcept
{
    const auto* slot = findSlot(ticket);
    if (slot == nullptr || !slot->occupied)
    {
        return {};
    }
    return slot->staging;
}

Core::u64 NullUploadLedger::userTag(UploadTicketId ticket) const noexcept
{
    const auto* slot = findSlot(ticket);
    if (slot == nullptr)
    {
        return 0;
    }
    return slot->userTag;
}

Core::Status NullUploadLedger::retire(UploadTicketId ticket) noexcept
{
    auto* slot = findSlot(ticket);
    if (slot == nullptr)
    {
        return Core::failure(RenderErrorCode::UploadTicketInvalid, "upload ticket is invalid or stale");
    }
    if (slot->state != UploadTicketState::Ready)
    {
        return Core::failure(RenderErrorCode::UploadTicketNotRetirable, "only Ready upload tickets can be retired");
    }
    if (m_ready > 0U)
    {
        --m_ready;
    }
    slot->staging.clear();
    slot->staging.shrink_to_fit();
    slot->state = UploadTicketState::Retired;
    slot->occupied = false;
    slot->userTag = 0;
    if (slot->generation == (std::numeric_limits<Core::u32>::max)())
    {
        slot->generation = 1;
    } else
    {
        ++slot->generation;
    }
    if (m_live > 0U)
    {
        --m_live;
    }
    return Core::success();
}

NullUploadLedger::Slot* NullUploadLedger::findSlot(UploadTicketId ticket) noexcept
{
    if (!ticket || ticket.index >= m_slots.size())
    {
        return nullptr;
    }
    auto& slot = m_slots[ticket.index];
    if (!slot.occupied || slot.generation != ticket.generation)
    {
        return nullptr;
    }
    return &slot;
}

const NullUploadLedger::Slot* NullUploadLedger::findSlot(UploadTicketId ticket) const noexcept
{
    if (!ticket || ticket.index >= m_slots.size())
    {
        return nullptr;
    }
    const auto& slot = m_slots[ticket.index];
    if (!slot.occupied || slot.generation != ticket.generation)
    {
        return nullptr;
    }
    return &slot;
}

} // namespace Tina::Render
