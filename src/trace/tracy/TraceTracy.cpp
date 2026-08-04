#include <tina/core/base/SourceLocation.hpp>
#include <tina/core/base/Types.hpp>

#include <tracy/Tracy.hpp>

#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <memory>

namespace Tina::Core::Trace::Detail {
namespace {

using TracyZone = tracy::ScopedZone;

static_assert(sizeof(TracyZone) <= 64);
static_assert(alignof(TracyZone) <= alignof(std::max_align_t));

} // namespace

void constructZone(
    void* storage,
    const usize storageSize,
    const char* name,
    const usize nameSize,
    const SourceLocation location) noexcept
{
    if (storage == nullptr || storageSize < sizeof(TracyZone)) {
        std::abort();
    }

    const char* const fileName = location.file_name();
    const char* const functionName = location.function_name();
    std::construct_at(
        static_cast<TracyZone*>(storage),
        location.line(),
        fileName,
        std::strlen(fileName),
        functionName,
        std::strlen(functionName),
        name,
        nameSize,
        0,
        -1,
        true);
}

void destroyZone(void* storage) noexcept
{
    if (storage == nullptr) {
        std::abort();
    }
    std::destroy_at(static_cast<TracyZone*>(storage));
}

} // namespace Tina::Core::Trace::Detail
