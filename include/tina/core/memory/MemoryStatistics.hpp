#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::Core {

struct MemoryStatistics final {
    usize currentBytes = 0;
    usize peakBytes = 0;
    u64 allocationCount = 0;
    u64 deallocationCount = 0;
    u64 failedAllocationCount = 0;
    u64 invalidDeallocationCount = 0;
};

} // namespace Tina::Core
