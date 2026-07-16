#include <tina/core/id/GenerationId.hpp>

#include <atomic>
#include <limits>

namespace Tina::Core {

std::optional<GenerationOwnerToken> GenerationOwnerToken::createUnique() noexcept
{
    static std::atomic<u64> nextOwner{1};
    constexpr u64 MaximumOwner = (std::numeric_limits<u32>::max)();

    u64 candidate = nextOwner.load(std::memory_order_relaxed);
    while (candidate <= MaximumOwner) {
        if (nextOwner.compare_exchange_weak(
                candidate,
                candidate + 1,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return GenerationOwnerToken(static_cast<u32>(candidate));
        }
    }
    return std::nullopt;
}

} // namespace Tina::Core
