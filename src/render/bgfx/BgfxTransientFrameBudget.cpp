#include "BgfxTransientFrameBudget.hpp"

#include <limits>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

[[nodiscard]] Core::Result<u32> requestBudget(BgfxTransientVertexRequest request)
{
    if (request.count == 0U)
    {
        return u32{0};
    }
    if (request.stride == 0U)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Transient vertex requests with a non-zero count must have a non-zero stride");
    }

    constexpr u64 MaxU32 = static_cast<u64>((std::numeric_limits<u32>::max)());
    const u64 stride = static_cast<u64>(request.stride);
    const u64 budget = (stride - 1ULL) + static_cast<u64>(request.count) * stride;
    if (budget > MaxU32)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Transient vertex request budget exceeds the u32 range");
    }

    return static_cast<u32>(budget);
}

} // namespace

Core::Result<u32> checkedTransientVertexBudget(std::span<const BgfxTransientVertexRequest> requests)
{
    constexpr u64 MaxU32 = static_cast<u64>((std::numeric_limits<u32>::max)());
    u64 totalBudget = 0U;

    for (const BgfxTransientVertexRequest& request : requests)
    {
        auto budget = requestBudget(request);
        if (!budget)
        {
            return Core::failure(std::move(budget.error()));
        }

        totalBudget += *budget;
        if (totalBudget > MaxU32)
        {
            return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                 "Transient vertex request budget exceeds the u32 range");
        }
    }

    return static_cast<u32>(totalBudget);
}

} // namespace Tina::Render::Bgfx
