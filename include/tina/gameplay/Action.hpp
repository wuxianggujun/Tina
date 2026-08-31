#pragma once

#include <tina/core/base/MoveOnlyFunction.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/GenerationId.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/gameplay/Easing.hpp>
#include <tina/gameplay/GameplayTypes.hpp>
#include <tina/math/Vec.hpp>

#include <concepts>
#include <memory_resource>
#include <span>
#include <type_traits>
#include <utility>

namespace Tina::Gameplay {

namespace Detail {
class ActionProgram;
struct ActionRunnerTag final {
};
} // namespace Detail

using ActionId = Core::GenerationId<Detail::ActionRunnerTag>;

// Receives eased progress and writes it wherever the game keeps the value. The
// alpha is already eased, and for overshooting curves it can leave [0,1] in the
// middle of a run -- that is the point of Back/Elastic, so it is not clamped.
using TweenApply = Core::MoveOnlyFunction<void(float)>;

// Hard authoring bound. A tree larger than this is almost always a loop building
// nodes rather than an authored intent, and an unbounded tree would let one
// gameplay event allocate without limit.
inline constexpr Core::usize MaximumActionNodeCount = 256;

struct ActionPlayOptions final {
    // Advances with the unscaled delta. Needed for anything that must keep
    // running while gameplay time is scaled to zero -- a pause menu's own
    // transitions being the usual case.
    bool ignoresTimeScale = false;
    bool startPaused = false;
};

// A composable, move-only description of timed work: tweens, delays, callbacks,
// and the sequence/parallel/repeat combinators over them.
//
// Authoring is fail-late by design. Every factory returns an Action rather than a
// Result, and an invalid argument poisons the value instead of stopping the
// expression; the first failure is what survives, and ActionRunner::play()
// reports it. Composing five tweens should not mean five error checks, and the
// alternative -- validating at play() only -- loses which subexpression was
// wrong. Poisoning holds an ErrorCode and a static message, so it allocates
// nothing on the failure path.
//
// An Action owns its node tree on the heap and is consumed by play(). Node
// storage is deliberately not drawn from the runner: authoring frequently happens
// while a callback of that same runner is executing, and handing out a slice of
// the runner's storage mid-dispatch is exactly the aliasing this design avoids.
class Action final {
  public:
    Action() noexcept = default;
    ~Action() noexcept;

    Action(const Action&) = delete;
    Action& operator=(const Action&) = delete;
    Action(Action&& other) noexcept;
    Action& operator=(Action&& other) noexcept;

    // True when this holds a node tree and no factory poisoned it.
    [[nodiscard]] bool hasValue() const noexcept;
    explicit operator bool() const noexcept { return hasValue(); }
    [[nodiscard]] bool failed() const noexcept { return m_failureMessage != nullptr; }
    [[nodiscard]] Core::ErrorCode failureCode() const noexcept { return m_failureCode; }
    // success() for a usable Action, otherwise the first authoring failure. The
    // Error is constructed here rather than stored, so the poisoned path stays
    // allocation-free until someone asks for it.
    [[nodiscard]] Core::Status status() const;
    [[nodiscard]] Core::usize nodeCount() const noexcept;

    // --- Leaves ---

    // Applies eased progress over `duration`. A zero duration applies exactly
    // once, at alpha 1, which is what "snap to the end" means.
    [[nodiscard]] static Action tween(Core::Duration duration, Easing easing, TweenApply apply);

    [[nodiscard]] static Action delay(Core::Duration duration);

    // Runs once when the action reaches it, then completes without consuming
    // time. Deliberately not a completion callback on play(): expressing "after
    // this, do that" with the same sequencing rule as everything else means there
    // is no second ordering contract to reconcile.
    template <typename Callable>
        requires std::is_invocable_v<Callable&> && (!std::same_as<std::remove_cvref_t<Callable>, Action>)
    [[nodiscard]] static Action call(Callable&& callable)
    {
        return tween(Core::Duration{0.0}, Easing::Linear,
                     CallAdapter<std::decay_t<Callable>>{std::forward<Callable>(callable)});
    }

    // --- Typed tweens ---
    //
    // The setter receives the interpolated value, not the alpha. `from` is
    // captured by value at authoring time: reading it at play() instead would
    // make an action's result depend on when it happened to start, which is the
    // hardest kind of animation bug to reproduce.

    template <typename Setter>
        requires std::is_invocable_v<Setter&, float>
    [[nodiscard]] static Action tweenFloat(Core::Duration duration, float from, float to,
                                           Easing easing, Setter&& setter)
    {
        return tween(duration, easing,
                     ValueAdapter<float, std::decay_t<Setter>>{from, to,
                                                               std::forward<Setter>(setter)});
    }

    template <typename Setter>
        requires std::is_invocable_v<Setter&, Math::Vec2>
    [[nodiscard]] static Action tweenVec2(Core::Duration duration, Math::Vec2 from, Math::Vec2 to,
                                          Easing easing, Setter&& setter)
    {
        return tween(duration, easing,
                     ValueAdapter<Math::Vec2, std::decay_t<Setter>>{from, to,
                                                                    std::forward<Setter>(setter)});
    }

    template <typename Setter>
        requires std::is_invocable_v<Setter&, Math::Vec3>
    [[nodiscard]] static Action tweenVec3(Core::Duration duration, Math::Vec3 from, Math::Vec3 to,
                                          Easing easing, Setter&& setter)
    {
        return tween(duration, easing,
                     ValueAdapter<Math::Vec3, std::decay_t<Setter>>{from, to,
                                                                    std::forward<Setter>(setter)});
    }

    template <typename Setter>
        requires std::is_invocable_v<Setter&, Math::Vec4>
    [[nodiscard]] static Action tweenVec4(Core::Duration duration, Math::Vec4 from, Math::Vec4 to,
                                          Easing easing, Setter&& setter)
    {
        return tween(duration, easing,
                     ValueAdapter<Math::Vec4, std::decay_t<Setter>>{from, to,
                                                                    std::forward<Setter>(setter)});
    }

    // --- Combinators ---
    //
    // Children are moved out of the arguments, so each is consumed exactly once.
    // Leftover time carries across a boundary: a 0.2s child followed by a 0.3s
    // child, advanced by 0.25s, finishes the first and puts 0.05s into the
    // second. Dropping that remainder is why hand-written sequences drift.

    [[nodiscard]] static Action sequence(std::span<Action> children);
    [[nodiscard]] static Action parallel(std::span<Action> children);

    template <typename... Children>
        requires(sizeof...(Children) >= 1)
        && (std::same_as<std::remove_cvref_t<Children>, Action> && ...)
    [[nodiscard]] static Action sequence(Children&&... children)
    {
        Action list[]{std::move(children)...};
        return sequence(std::span<Action>(list));
    }

    template <typename... Children>
        requires(sizeof...(Children) >= 1)
        && (std::same_as<std::remove_cvref_t<Children>, Action> && ...)
    [[nodiscard]] static Action parallel(Children&&... children)
    {
        Action list[]{std::move(children)...};
        return parallel(std::span<Action>(list));
    }

    // Re-runs `child` from its start. Repeat::forever() is legal; a repeated
    // subtree that consumes no time is bounded per advance by the runner rather
    // than spinning, and the bound is reported in stats.
    [[nodiscard]] static Action repeat(Repeat repeat, Action child);

  private:
    friend class ActionRunner;

    // Adapters instead of lambdas so a MoveOnlyFunction target's size is a
    // function of the setter alone. A capturing lambda around an already-erased
    // callable would be larger than the inline buffer and force an allocation.
    template <typename Value, typename Setter>
    struct ValueAdapter final {
        Value from{};
        Value to{};
        Setter setter;

        void operator()(float alpha) { setter(interpolate(from, to, alpha)); }
    };

    template <typename Callable>
    struct CallAdapter final {
        Callable callable;

        void operator()(float) { callable(); }
    };

    Action(Core::ErrorCode code, const char* message) noexcept;

    // Shared by sequence() and parallel(), which differ only in the node kind they
    // emit. A bool rather than the private node enum so the public header does not
    // have to name an implementation type.
    [[nodiscard]] static Action combine(std::span<Action> children, bool sequential);

    void reset() noexcept;

    Detail::ActionProgram* m_program = nullptr;
    Core::ErrorCode m_failureCode{};
    // Always a string literal, so poisoning never allocates.
    const char* m_failureMessage = nullptr;
};

struct ActionRunnerConfig final {
    // Concurrently playing actions. Fixed at Create; exceeding it is
    // CapacityExceeded rather than a reallocation.
    Core::usize actionCapacity = 128;
    // Upper bound on how many times one Repeat node may restart its child within
    // a single advance(). A repeated subtree whose total duration rounds to zero
    // would otherwise never return, and the interesting part is that it looks
    // exactly like a hang rather than like a content error -- so it is bounded
    // and counted instead.
    Core::u32 maximumRepeatIterationsPerAdvance = 64;
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct ActionRunnerStats final {
    Core::usize actionCapacity = 0;
    Core::usize activeActionCount = 0;
    Core::usize activeActionHighWater = 0;
    Core::u64 advanceCount = 0;
    Core::u64 startedCount = 0;
    Core::u64 completedCount = 0;
    Core::u64 cancelledCount = 0;
    // Repeat restarts refused by maximumRepeatIterationsPerAdvance. Non-zero
    // means some action is repeating faster than it can be observed, not that
    // anything failed.
    Core::u64 clampedRepeatIterations = 0;
};

// Fixed-capacity owner-thread player for Action trees.
//
// Time arrives as an explicit delta, for the same reason the Scheduler's does:
// the frame loop owns the fixed/frame split (ADR 0015), and an object that
// sampled its own clock could not be driven from fixedUpdate.
//
// Not thread-safe and single-owner. Callbacks may play and cancel freely,
// including cancelling the action they are running inside: that cancel takes
// effect at the next node boundary, so no further node of the cancelled action
// runs, and the tree stays alive until the recursion has unwound. An action
// played from inside a callback first advances on the *next* advance(), so
// dispatch order never depends on how deeply the callbacks nested.
class ActionRunner final {
  public:
    [[nodiscard]] static Core::Result<ActionRunner> Create(ActionRunnerConfig config = {});

    ~ActionRunner() noexcept;

    ActionRunner(const ActionRunner&) = delete;
    ActionRunner& operator=(const ActionRunner&) = delete;
    ActionRunner(ActionRunner&& other) noexcept;
    ActionRunner& operator=(ActionRunner&& other) noexcept;

    // Consumes the action. A poisoned action fails with the authoring error it
    // recorded, so the diagnostic names the subexpression that was wrong rather
    // than the play call. The action is not started here: its first node runs at
    // the next advance(), which keeps every action's first frame identical
    // regardless of where in the frame it was played.
    [[nodiscard]] Core::Result<ActionId> play(Action action, ActionPlayOptions options = {});

    // Stale or unknown ids are InvalidHandle: a double cancel usually means two
    // owners each believe they hold the action.
    [[nodiscard]] Core::Status cancel(ActionId action);
    // Destroys every setter and callback, releasing whatever they captured.
    void cancelAll() noexcept;

    [[nodiscard]] Core::Status setPaused(ActionId action, bool paused);
    [[nodiscard]] Core::Result<bool> isPaused(ActionId action) const;
    [[nodiscard]] bool isPlaying(ActionId action) const noexcept;

    // Zero is a full pause and is valid; negative and non-finite are rejected.
    [[nodiscard]] Core::Status setTimeScale(double scale);
    [[nodiscard]] double timeScale() const noexcept;

    // Advances every unpaused action and retires the ones that completed.
    // Re-entering from a callback is ReentrantDispatch and changes nothing.
    [[nodiscard]] Core::Status advance(Core::Duration delta);

    [[nodiscard]] Core::usize activeCount() const noexcept;
    [[nodiscard]] ActionRunnerStats stats() const noexcept;

  private:
    struct Impl;

    explicit ActionRunner(Impl* impl) noexcept;

    Impl* m_impl = nullptr;
};

} // namespace Tina::Gameplay
