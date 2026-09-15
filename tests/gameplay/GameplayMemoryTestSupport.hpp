#pragma once

#include <tina/core/base/MoveOnlyFunction.hpp>
#include <tina/core/base/Types.hpp>

#include <limits>
#include <memory_resource>
#include <new>
#include <utility>

namespace Tina::Gameplay::TestSupport {

struct DestructorCallback final {
    explicit DestructorCallback(Core::MoveOnlyFunction<void()> function) : callback(std::move(function)) {}
    DestructorCallback(DestructorCallback&&) noexcept = default;
    DestructorCallback(const DestructorCallback&) = delete;
    ~DestructorCallback() noexcept { if (callback) { callback(); } }
    Core::MoveOnlyFunction<void()> callback;
};

class FailingMemoryResource final : public std::pmr::memory_resource {
public:
    Core::usize failAt = (std::numeric_limits<Core::usize>::max)();
    Core::usize allocations = 0;
    Core::usize outstandingBytes = 0;

private:
    void* do_allocate(Core::usize bytes, Core::usize alignment) override
    {
        if (allocations++ == failAt) { throw std::bad_alloc{}; }
        void* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        outstandingBytes += bytes;
        return result;
    }
    void do_deallocate(void* pointer, Core::usize bytes, Core::usize alignment) override
    {
        outstandingBytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

} // namespace Tina::Gameplay::TestSupport
