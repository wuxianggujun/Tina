#pragma once

#if defined(TINA_TRACE_BACKEND_NONE) && defined(TINA_TRACE_BACKEND_ENABLED)
#error "Multiple Tina trace backends are selected"
#elif defined(TINA_TRACE_BACKEND_NONE)
#if TINA_TRACE_BACKEND_NONE != 1
#error "TINA_TRACE_BACKEND_NONE must be defined as 1"
#endif

#define TINA_TRACE_ZONE(nameLiteral) \
    do {                              \
    } while (false)

#elif defined(TINA_TRACE_BACKEND_ENABLED)
#if TINA_TRACE_BACKEND_ENABLED != 1
#error "TINA_TRACE_BACKEND_ENABLED must be defined as 1"
#endif

#include <tina/core/base/SourceLocation.hpp>
#include <tina/core/base/Types.hpp>

#include <cstddef>

namespace Tina::Core::Trace::Detail {

void constructZone(
    void* storage,
    usize storageSize,
    const char* name,
    usize nameLength,
    SourceLocation sourceLocation) noexcept;
void destroyZone(void* storage) noexcept;

class Zone final {
public:
    template <usize NameSize>
    Zone(
        const char (&name)[NameSize],
        usize nameLength,
        SourceLocation sourceLocation) noexcept
    {
        static_assert(NameSize > 0U);
        constructZone(storage_, sizeof(storage_), name, nameLength, sourceLocation);
    }

    ~Zone() noexcept
    {
        destroyZone(storage_);
    }

    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;
    Zone(Zone&&) = delete;
    Zone& operator=(Zone&&) = delete;

private:
    alignas(std::max_align_t) std::byte storage_[64]{};
};

} // namespace Tina::Core::Trace::Detail

#define TINA_DETAIL_TRACE_JOIN_IMPL(left, right) left##right
#define TINA_DETAIL_TRACE_JOIN(left, right) TINA_DETAIL_TRACE_JOIN_IMPL(left, right)
#define TINA_TRACE_ZONE(nameLiteral)                                                   \
    ::Tina::Core::Trace::Detail::Zone                                                 \
        TINA_DETAIL_TRACE_JOIN(tinaTraceZone_, __COUNTER__){                          \
            (nameLiteral),                                                            \
            sizeof(nameLiteral) - 1U,                                                 \
            ::Tina::Core::SourceLocation::current()}

#else
#error "Tina trace backend selection is missing"
#endif
