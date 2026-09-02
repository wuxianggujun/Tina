#include "Html5TouchSlotTable.hpp"

#include <algorithm>

namespace Tina::Platform {

Html5TouchSlotTable::Html5TouchSlotTable() noexcept
{
    for (Entry& entry : entries_)
    {
        entry.identifier = UnusedIdentifier;
    }
}

PointerId Html5TouchSlotTable::acquire(int identifier) noexcept
{
    if (identifier == UnusedIdentifier)
    {
        // The sentinel is not a usable identity. Browsers do not produce it, but accepting it
        // would make the slot indistinguishable from a free one.
        return InvalidSlot;
    }

    const PointerId existing = find(identifier);
    if (existing != InvalidSlot)
    {
        return existing;
    }

    for (usize slot = 0; slot < entries_.size(); ++slot)
    {
        if (entries_[slot].identifier == UnusedIdentifier)
        {
            entries_[slot].identifier = identifier;
            entries_[slot].lastLogicalX = 0.0;
            entries_[slot].lastLogicalY = 0.0;
            return static_cast<PointerId>(slot);
        }
    }
    // Genuinely more simultaneous fingers than PointerCapacity. Reporting it lets the caller
    // drop the event rather than evicting a finger that is still down.
    return InvalidSlot;
}

PointerId Html5TouchSlotTable::find(int identifier) const noexcept
{
    if (identifier == UnusedIdentifier)
    {
        return InvalidSlot;
    }
    for (usize slot = 0; slot < entries_.size(); ++slot)
    {
        if (entries_[slot].identifier == identifier)
        {
            return static_cast<PointerId>(slot);
        }
    }
    return InvalidSlot;
}

void Html5TouchSlotTable::release(int identifier) noexcept
{
    const PointerId slot = find(identifier);
    if (slot != InvalidSlot)
    {
        entries_[slot].identifier = UnusedIdentifier;
    }
}

void Html5TouchSlotTable::releaseAll() noexcept
{
    for (Entry& entry : entries_)
    {
        entry.identifier = UnusedIdentifier;
    }
}

bool Html5TouchSlotTable::ownsSlot(PointerId slot) const noexcept
{
    return slot < entries_.size() && entries_[slot].identifier != UnusedIdentifier;
}

void Html5TouchSlotTable::setLastPosition(PointerId slot, double logicalX, double logicalY) noexcept
{
    if (slot >= entries_.size())
    {
        return;
    }
    entries_[slot].lastLogicalX = logicalX;
    entries_[slot].lastLogicalY = logicalY;
}

bool Html5TouchSlotTable::lastPosition(PointerId slot, double& logicalX, double& logicalY) const noexcept
{
    if (slot >= entries_.size() || entries_[slot].identifier == UnusedIdentifier)
    {
        return false;
    }
    logicalX = entries_[slot].lastLogicalX;
    logicalY = entries_[slot].lastLogicalY;
    return true;
}

} // namespace Tina::Platform
