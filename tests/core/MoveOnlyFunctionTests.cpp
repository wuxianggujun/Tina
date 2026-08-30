#include <tina/core/base/MoveOnlyFunction.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace Tina::Tests {
namespace {

TEST(MoveOnlyFunctionTest, EmptyIsFalseAndAssignedIsTrue)
{
    Core::MoveOnlyFunction<int()> empty;
    EXPECT_FALSE(static_cast<bool>(empty));

    Core::MoveOnlyFunction<int()> assigned{[] { return 7; }};
    ASSERT_TRUE(static_cast<bool>(assigned));
    EXPECT_EQ(assigned(), 7);

    assigned = nullptr;
    EXPECT_FALSE(static_cast<bool>(assigned));
}

// The reason std::function cannot be used here: every engine factory returns or captures
// a unique_ptr, so the target is move-only by construction.
TEST(MoveOnlyFunctionTest, HoldsAMoveOnlyCaptureAndTransfersOwnership)
{
    auto owned = std::make_unique<int>(41);
    Core::MoveOnlyFunction<int()> callable{[held = std::move(owned)] { return *held + 1; }};
    ASSERT_TRUE(static_cast<bool>(callable));

    Core::MoveOnlyFunction<int()> moved{std::move(callable)};
    EXPECT_FALSE(static_cast<bool>(callable)) << "a moved-from function must be empty";
    ASSERT_TRUE(static_cast<bool>(moved));
    EXPECT_EQ(moved(), 42);
}

TEST(MoveOnlyFunctionTest, MoveAssignmentDestroysThePreviousTarget)
{
    int destroyed = 0;
    struct Tracker final {
        int* counter;
        Tracker(int* target) noexcept : counter(target) {}
        Tracker(Tracker&& other) noexcept : counter(std::exchange(other.counter, nullptr)) {}
        Tracker(const Tracker&) = delete;
        ~Tracker()
        {
            if (counter != nullptr)
            {
                ++*counter;
            }
        }
        void operator()() const noexcept {}
    };

    Core::MoveOnlyFunction<void()> first{Tracker{&destroyed}};
    Core::MoveOnlyFunction<void()> second{Tracker{&destroyed}};
    // Overwriting must run the replaced target's destructor exactly once, or a queued
    // work item's captures would leak.
    second = std::move(first);
    EXPECT_EQ(destroyed, 1);

    second = nullptr;
    EXPECT_EQ(destroyed, 2);
}

// Targets are allowed to mutate their own captures, matching std::move_only_function's
// non-const operator(). A task queue pops a work item and calls it once.
TEST(MoveOnlyFunctionTest, TargetCanMutateItsOwnCaptures)
{
    Core::MoveOnlyFunction<int()> counter{[count = 0]() mutable { return ++count; }};
    EXPECT_EQ(counter(), 1);
    EXPECT_EQ(counter(), 2);
    EXPECT_EQ(counter(), 3);
}

TEST(MoveOnlyFunctionTest, ForwardsArgumentsAndReturnsByValue)
{
    Core::MoveOnlyFunction<std::string(std::string, int)> join{
        [](std::string text, int suffix) { return text + std::to_string(suffix); }};
    EXPECT_EQ(join("frame", 42), "frame42");

    Core::MoveOnlyFunction<void(std::unique_ptr<int>&)> consume{
        [](std::unique_ptr<int>& slot) { slot.reset(); }};
    auto owned = std::make_unique<int>(1);
    consume(owned);
    EXPECT_EQ(owned, nullptr) << "a by-reference argument must reach the target unchanged";
}

// The capture that sized the inline buffer: an AssetSystem IO work item holds a
// std::string path plus a request pointer and a byte budget. It must not reach the heap
// path, or the common case pays an allocation per scheduled task.
TEST(MoveOnlyFunctionTest, FitsTheWidestRealEngineCapture)
{
    struct Request final {
        int id = 0;
    };
    Request request{.id = 5};
    std::string path = "assets/cooked/texture_00.tasset";
    const Core::u64 maxBytes = 64ULL * 1024ULL;

    auto capture = [target = &request, owned = std::move(path), maxBytes]() noexcept {
        target->id = static_cast<int>(owned.size() + maxBytes);
    };
    static_assert(sizeof(capture) <= Core::MoveOnlyFunctionInlineCapacity,
                  "the widest real engine work item must stay in the inline buffer");
    static_assert(alignof(decltype(capture)) <= Core::MoveOnlyFunctionInlineAlignment);

    Core::MoveOnlyFunction<void()> ioWork{std::move(capture)};
    ASSERT_TRUE(static_cast<bool>(ioWork));
    ioWork();
    EXPECT_EQ(request.id, static_cast<int>(std::string{"assets/cooked/texture_00.tasset"}.size() + maxBytes));
}

// The case that proved an inline-only design impossible: TaskGroup::add wraps a caller's
// callable in another one to attach completion bookkeeping, so the outer target contains a
// whole MoveOnlyFunction and is therefore always larger than the buffer it would go in.
TEST(MoveOnlyFunctionTest, WrappingAnotherFunctionTakesTheHeapPathAndStillRuns)
{
    using Work = Core::MoveOnlyFunction<void()>;
    struct Wrapper final {
        Work inner;
        int* finished;
        void operator()()
        {
            inner();
            ++*finished;
        }
    };
    static_assert(sizeof(Wrapper) > Core::MoveOnlyFunctionInlineCapacity,
                  "a wrapped work item cannot fit inline, whatever the capacity");

    int ran = 0;
    int finished = 0;
    Work outer{Wrapper{Work{[&ran] { ++ran; }}, &finished}};

    // Moving must carry the heap target across rather than copying it, and the pointer
    // move is what keeps the move noexcept.
    Work queued{std::move(outer)};
    EXPECT_FALSE(static_cast<bool>(outer));
    ASSERT_TRUE(static_cast<bool>(queued));

    queued();
    EXPECT_EQ(ran, 1);
    EXPECT_EQ(finished, 1);
}

// Moving out of an inline target must run the husk's destructor. A capture that releases a
// resource in ~T() cannot express that by nulling a member during its own move, so this is
// the only test here that would notice the husk being abandoned.
TEST(MoveOnlyFunctionTest, MovingAnInlineTargetDestroysTheHusk)
{
    int alive = 0;
    struct Lease final {
        int* population;
        Lease(int* target) noexcept : population(target)
        {
            ++*population;
        }
        // A real lease counts every live copy: the moved-from one is still constructed and
        // still owes a release, which is exactly what a moved-from husk is.
        Lease(Lease&& other) noexcept : population(other.population)
        {
            ++*population;
        }
        Lease(const Lease&) = delete;
        ~Lease()
        {
            --*population;
        }
        void operator()() const noexcept {}
    };

    Core::MoveOnlyFunction<void()> owner{Lease{&alive}};
    ASSERT_EQ(alive, 1);

    Core::MoveOnlyFunction<void()> moved{std::move(owner)};
    EXPECT_EQ(alive, 1) << "the moved-from husk must be destroyed, not left holding the lease";
    EXPECT_FALSE(static_cast<bool>(owner));

    moved = nullptr;
    EXPECT_EQ(alive, 0);
}

TEST(MoveOnlyFunctionTest, DestroysAHeapTargetExactlyOnce)
{
    int destroyed = 0;
    struct Oversized final {
        int* counter;
        // Deliberately past the inline buffer so this target is heap-allocated.
        unsigned char padding[Core::MoveOnlyFunctionInlineCapacity]{};
        Oversized(int* target) noexcept : counter(target) {}
        Oversized(Oversized&& other) noexcept : counter(std::exchange(other.counter, nullptr)) {}
        Oversized(const Oversized&) = delete;
        ~Oversized()
        {
            if (counter != nullptr)
            {
                ++*counter;
            }
        }
        void operator()() const noexcept {}
    };
    static_assert(sizeof(Oversized) > Core::MoveOnlyFunctionInlineCapacity);

    {
        Core::MoveOnlyFunction<void()> owner{Oversized{&destroyed}};
        Core::MoveOnlyFunction<void()> moved{std::move(owner)};
        EXPECT_EQ(destroyed, 0) << "a move must transfer the heap target, not free it";
    }
    EXPECT_EQ(destroyed, 1) << "the heap target must be deleted once, by the last owner";
}

} // namespace
} // namespace Tina::Tests
