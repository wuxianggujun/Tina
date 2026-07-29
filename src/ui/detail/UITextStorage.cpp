#include "UITextStorage.hpp"

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>

namespace Tina::UI::Detail {

UITextStorage::UITextStorage(usize byteCapacity, usize freeAllocationCapacity, std::pmr::memory_resource& resource)
    : bytes_(&resource), freeAllocations_(&resource)
{
    bytes_.resize(byteCapacity, '\0');
    freeAllocations_.reserve(freeAllocationCapacity);
}

Core::Result<UITextStorage::Allocation> UITextStorage::allocate(u32 byteCount)
{
    if (byteCount == 0)
    {
        return Allocation{};
    }

    for (usize freeIndex = 0; freeIndex < freeAllocations_.size(); ++freeIndex)
    {
        Allocation& candidate = freeAllocations_[freeIndex];
        if (candidate.capacity < byteCount)
        {
            continue;
        }

        const Allocation allocated{
            .offset = candidate.offset,
            .capacity = byteCount,
        };
        candidate.offset += byteCount;
        candidate.capacity -= byteCount;
        if (candidate.capacity == 0)
        {
            freeAllocations_[freeIndex] = freeAllocations_.back();
            freeAllocations_.pop_back();
        }
        used_ += byteCount;
        highWater_ = (std::max)(highWater_, used_);
        return allocated;
    }

    if (bumpOffset_ > bytes_.size() || static_cast<usize>(byteCount) > bytes_.size() - bumpOffset_)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI text byte capacity has been exhausted");
    }

    const Allocation allocated{
        .offset = static_cast<u32>(bumpOffset_),
        .capacity = byteCount,
    };
    bumpOffset_ += byteCount;
    used_ += byteCount;
    highWater_ = (std::max)(highWater_, used_);
    return allocated;
}

void UITextStorage::release(Allocation allocation) noexcept
{
    if (allocation.capacity == 0)
    {
        return;
    }

    if (used_ >= allocation.capacity)
    {
        used_ -= allocation.capacity;
    } else
    {
        used_ = 0;
    }

    usize mergedBegin = allocation.offset;
    usize mergedEnd = static_cast<usize>(allocation.offset) + allocation.capacity;
    bool mergedAnotherBlock = true;
    while (mergedAnotherBlock)
    {
        mergedAnotherBlock = false;
        for (usize index = 0; index < freeAllocations_.size();)
        {
            const Allocation candidate = freeAllocations_[index];
            const usize candidateBegin = candidate.offset;
            const usize candidateEnd = static_cast<usize>(candidate.offset) + candidate.capacity;
            if (candidateEnd < mergedBegin || candidateBegin > mergedEnd)
            {
                ++index;
                continue;
            }

            mergedBegin = (std::min)(mergedBegin, candidateBegin);
            mergedEnd = (std::max)(mergedEnd, candidateEnd);
            freeAllocations_[index] = freeAllocations_.back();
            freeAllocations_.pop_back();
            mergedAnotherBlock = true;
        }
    }

    if (mergedEnd == bumpOffset_)
    {
        bumpOffset_ = mergedBegin;
        return;
    }

    assert(mergedEnd <= bytes_.size());
    freeAllocations_.push_back(Allocation{
        .offset = static_cast<u32>(mergedBegin),
        .capacity = static_cast<u32>(mergedEnd - mergedBegin),
    });
}

void UITextStorage::write(Allocation allocation, std::string_view text) noexcept
{
    const usize offset = allocation.offset;
    assert(text.size() <= allocation.capacity);
    assert(offset <= bytes_.size());
    assert(text.size() <= bytes_.size() - offset);
    if (text.empty() || text.size() > allocation.capacity || offset > bytes_.size() ||
        text.size() > bytes_.size() - offset)
    {
        return;
    }
    std::memcpy(bytes_.data() + offset, text.data(), text.size());
}

std::string_view UITextStorage::view(Allocation allocation, u32 length) const noexcept
{
    const usize offset = allocation.offset;
    assert(length <= allocation.capacity);
    assert(offset <= bytes_.size());
    assert(static_cast<usize>(length) <= bytes_.size() - offset);
    if (length == 0 || length > allocation.capacity || offset > bytes_.size() ||
        static_cast<usize>(length) > bytes_.size() - offset)
    {
        return {};
    }
    return std::string_view(bytes_.data() + offset, length);
}

usize UITextStorage::capacity() const noexcept
{
    return bytes_.size();
}

usize UITextStorage::used() const noexcept
{
    return used_;
}

usize UITextStorage::highWater() const noexcept
{
    return highWater_;
}

} // namespace Tina::UI::Detail
