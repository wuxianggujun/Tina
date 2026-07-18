#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <compare>
#include <memory_resource>
#include <optional>
#include <span>

namespace Tina::Render {

struct UIPixelRect final {
    i32 x = 0;
    i32 y = 0;
    u32 width = 0;
    u32 height = 0;

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return width == 0 || height == 0;
    }

    auto operator<=>(const UIPixelRect&) const = default;
};

struct UIPremultipliedRgba8 final {
    u8 red = 0;
    u8 green = 0;
    u8 blue = 0;
    u8 alpha = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return red <= alpha && green <= alpha && blue <= alpha;
    }

    [[nodiscard]] constexpr bool transparent() const noexcept
    {
        return alpha == 0;
    }

    auto operator<=>(const UIPremultipliedRgba8&) const = default;
};

class UIClipId final {
  public:
    constexpr UIClipId() noexcept = default;

    [[nodiscard]] constexpr bool hasClip() const noexcept
    {
        return m_value != 0;
    }

    explicit constexpr operator bool() const noexcept
    {
        return hasClip();
    }

    auto operator<=>(const UIClipId&) const = default;

  private:
    friend class UIDisplayListBuilder;
    friend class UIDisplayListView;

    explicit constexpr UIClipId(u32 value) noexcept : m_value(value)
    {
    }

    u32 m_value = 0;
};

enum class UIDrawCommandKind : u8 {
    SolidQuad,
};

struct UISolidQuadInput final {
    u32 paintOrdinal = 0;
    UIPixelRect bounds{};
    UIPremultipliedRgba8 color{};
    std::optional<UIPixelRect> effectiveClip{};
};

struct UIDrawCommand final {
    UIDrawCommandKind kind = UIDrawCommandKind::SolidQuad;
    u32 paintOrdinal = 0;
    UIPixelRect bounds{};
    UIPremultipliedRgba8 color{};
    UIClipId clip{};
};

struct UIDrawBatch final {
    UIDrawCommandKind kind = UIDrawCommandKind::SolidQuad;
    UIClipId clip{};
    u32 firstCommand = 0;
    u32 commandCount = 0;
};

struct UIDisplayListCapacity final {
    u32 commandCount = 0;
    u32 clipCount = 0;
    u32 batchCount = 0;
};

struct UIDisplayListStatistics final {
    u32 solidQuadCommandCount = 0;
    u32 clipCount = 0;
    u32 batchCount = 0;
    u32 prunedEmptyBoundsCount = 0;
    u32 prunedTransparentCount = 0;
    u32 prunedEmptyClipCount = 0;
    u32 prunedOutsideClipCount = 0;
    u64 paintOrderChecksum = 0;
};

struct UIDisplayListBuilderStatistics final {
    u64 begunBuildCount = 0;
    u64 committedBuildCount = 0;
    u64 rolledBackBuildCount = 0;
    u64 capacityFailureCount = 0;
    u64 invalidInputFailureCount = 0;
};

// Borrowed, frame-local view. beginFrame() immediately invalidates the previous
// published view because the builder owns one fixed storage buffer. A failed
// replacement build publishes neither the old view nor a truncated new view.
// Moving or destroying the builder also invalidates every borrowed view.
class UIDisplayListView final {
  public:
    constexpr UIDisplayListView() noexcept = default;

    [[nodiscard]] constexpr std::span<const UIDrawCommand> commands() const noexcept
    {
        return m_commands;
    }

    [[nodiscard]] constexpr std::span<const UIPixelRect> clips() const noexcept
    {
        return m_clips;
    }

    [[nodiscard]] constexpr std::span<const UIDrawBatch> batches() const noexcept
    {
        return m_batches;
    }

    [[nodiscard]] constexpr const UIDisplayListStatistics& statistics() const noexcept
    {
        return m_statistics;
    }

    [[nodiscard]] constexpr u64 paintOrderChecksum() const noexcept
    {
        return m_statistics.paintOrderChecksum;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return m_commands.empty();
    }

    [[nodiscard]] const UIPixelRect* resolveClip(UIClipId clip) const noexcept;

  private:
    friend class UIDisplayListBuilder;

    constexpr UIDisplayListView(std::span<const UIDrawCommand> commands, std::span<const UIPixelRect> clips,
                                std::span<const UIDrawBatch> batches, UIDisplayListStatistics statistics) noexcept
        : m_commands(commands), m_clips(clips), m_batches(batches), m_statistics(statistics)
    {
    }

    std::span<const UIDrawCommand> m_commands{};
    std::span<const UIPixelRect> m_clips{};
    std::span<const UIDrawBatch> m_batches{};
    UIDisplayListStatistics m_statistics{};
};

class UIDisplayListBuilder final {
  public:
    // storage owns all fixed command/clip/batch blocks and must outlive the
    // builder and every borrowed view returned by it.
    [[nodiscard]] static Core::Result<UIDisplayListBuilder>
    Create(UIDisplayListCapacity capacity, std::pmr::memory_resource& storage = *std::pmr::get_default_resource());

    UIDisplayListBuilder(const UIDisplayListBuilder&) = delete;
    UIDisplayListBuilder& operator=(const UIDisplayListBuilder&) = delete;
    UIDisplayListBuilder(UIDisplayListBuilder&& other) noexcept;
    UIDisplayListBuilder& operator=(UIDisplayListBuilder&&) = delete;
    ~UIDisplayListBuilder();

    [[nodiscard]] Core::Status beginFrame();
    [[nodiscard]] Core::Status addSolidQuad(const UISolidQuadInput& input);
    [[nodiscard]] Core::Result<UIDisplayListView> commit();
    void rollback() noexcept;

    [[nodiscard]] UIDisplayListView publishedView() const noexcept;
    [[nodiscard]] UIDisplayListCapacity capacity() const noexcept;
    [[nodiscard]] UIDisplayListBuilderStatistics statistics() const noexcept;

  private:
    enum class State : u8 {
        Ready,
        Building,
        Published,
    };

    UIDisplayListBuilder(UIDisplayListCapacity capacity, std::pmr::memory_resource& storage, UIDrawCommand* commands,
                         UIPixelRect* clips, UIDrawBatch* batches) noexcept;

    [[nodiscard]] Core::Status failBuild(Core::ErrorCode code, const char* message);
    [[nodiscard]] static u64 calculatePaintOrderChecksum(std::span<const UIDrawCommand> commands,
                                                         std::span<const UIPixelRect> clips) noexcept;
    [[nodiscard]] UIClipId findClip(const UIPixelRect& clip) const noexcept;
    [[nodiscard]] bool hasCapacityFor(bool needsClip, bool needsBatch) const noexcept;
    void clearCandidate() noexcept;
    void releaseStorage() noexcept;
    void rollbackBuilding() noexcept;

    UIDisplayListCapacity m_capacity{};
    std::pmr::memory_resource* m_storage = nullptr;
    UIDrawCommand* m_commands = nullptr;
    UIPixelRect* m_clips = nullptr;
    UIDrawBatch* m_batches = nullptr;
    u32 m_commandCount = 0;
    u32 m_clipCount = 0;
    u32 m_batchCount = 0;
    UIDisplayListStatistics m_candidateStatistics{};
    UIDisplayListStatistics m_publishedStatistics{};
    UIDisplayListBuilderStatistics m_statistics{};
    std::optional<Core::ErrorCode> m_stickyBuildError{};
    std::optional<u32> m_lastPaintOrdinal{};
    State m_state = State::Ready;
};

} // namespace Tina::Render
