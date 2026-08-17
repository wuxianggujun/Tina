#include <tina/render/UIDisplayList.hpp>

#include <tina/render/RenderErrors.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

namespace Tina::Render {
namespace {

[[nodiscard]] Core::Status displayListFailure(Core::ErrorCode code, std::string_view message)
{
    return Core::failure(code, message);
}

[[nodiscard]] bool intersects(const UIPixelRect& left, const UIPixelRect& right) noexcept
{
    const i64 leftRight = static_cast<i64>(left.x) + static_cast<i64>(left.width);
    const i64 leftBottom = static_cast<i64>(left.y) + static_cast<i64>(left.height);
    const i64 rightRight = static_cast<i64>(right.x) + static_cast<i64>(right.width);
    const i64 rightBottom = static_cast<i64>(right.y) + static_cast<i64>(right.height);
    return static_cast<i64>(left.x) < rightRight && static_cast<i64>(right.x) < leftRight &&
           static_cast<i64>(left.y) < rightBottom && static_cast<i64>(right.y) < leftBottom;
}

[[nodiscard]] constexpr std::array<UISubpixelPoint, 4> quadPoints(
    const UISolidQuadVertices& vertices) noexcept
{
    return {
        vertices.topLeft,
        vertices.topRight,
        vertices.bottomRight,
        vertices.bottomLeft,
    };
}

[[nodiscard]] bool validCornerRadii(const UIPixelCornerRadii& radii,
                                    float maximumRadius) noexcept
{
    const std::array values{
        radii.topLeft,
        radii.topRight,
        radii.bottomRight,
        radii.bottomLeft,
    };
    return std::ranges::all_of(values, [maximumRadius](float radius) noexcept {
        return std::isfinite(radius) && radius >= 0.0F && radius <= maximumRadius;
    });
}

[[nodiscard]] bool validSolidQuadVertices(
    const UISolidQuadVertices& vertices,
    const UIPixelRect& bounds) noexcept
{
    const std::array points = quadPoints(vertices);
    const double left = static_cast<double>(bounds.x);
    const double top = static_cast<double>(bounds.y);
    const double right = left + static_cast<double>(bounds.width);
    const double bottom = top + static_cast<double>(bounds.height);
    for (const UISubpixelPoint point : points)
    {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            static_cast<double>(point.x) < left ||
            static_cast<double>(point.x) > right ||
            static_cast<double>(point.y) < top ||
            static_cast<double>(point.y) > bottom)
        {
            return false;
        }
    }

    int winding = 0;
    for (usize index = 0; index < points.size(); ++index)
    {
        const UISubpixelPoint current = points[index];
        const UISubpixelPoint next = points[(index + 1U) % points.size()];
        const UISubpixelPoint following = points[(index + 2U) % points.size()];
        const double firstX = static_cast<double>(next.x) - current.x;
        const double firstY = static_cast<double>(next.y) - current.y;
        const double secondX = static_cast<double>(following.x) - next.x;
        const double secondY = static_cast<double>(following.y) - next.y;
        const double cross = firstX * secondY - firstY * secondX;
        if (!std::isfinite(cross) || cross == 0.0)
        {
            return false;
        }
        const int edgeWinding = cross > 0.0 ? 1 : -1;
        if (winding == 0)
        {
            winding = edgeWinding;
        }
        else if (winding != edgeWinding)
        {
            return false;
        }
    }
    return true;
}

inline constexpr u64 Fnv1aOffsetBasis = 14695981039346656037ULL;
inline constexpr u64 Fnv1aPrime = 1099511628211ULL;

void hashByte(u64& checksum, u8 value) noexcept
{
    checksum ^= value;
    checksum *= Fnv1aPrime;
}

void hashU32(u64& checksum, u32 value) noexcept
{
    for (u32 shift = 0; shift < 32; shift += 8)
    {
        hashByte(checksum, static_cast<u8>((value >> shift) & 0xFFU));
    }
}

} // namespace

const UIPixelRect* UIDisplayListView::resolveClip(UIClipId clip) const noexcept
{
    if (!clip.hasClip())
    {
        return nullptr;
    }

    const usize index = static_cast<usize>(clip.m_value - 1U);
    if (index >= m_clips.size())
    {
        return nullptr;
    }
    return &m_clips[index];
}

Core::Result<UIDisplayListBuilder> UIDisplayListBuilder::Create(UIDisplayListCapacity capacity,
                                                                std::pmr::memory_resource& storage)
{
    if (capacity.commandCount == 0 || capacity.batchCount == 0)
    {
        return Core::failure(RenderErrorCode::InvalidDisplayListCapacity,
                             "UI DisplayList command and batch capacities must be non-zero");
    }

    const auto allocationSizeIsValid = [](u32 count, usize elementSize) noexcept {
        return static_cast<usize>(count) <= (std::numeric_limits<usize>::max)() / elementSize;
    };
    if (!allocationSizeIsValid(capacity.commandCount, sizeof(UIDrawCommand)) ||
        !allocationSizeIsValid(capacity.clipCount, sizeof(UIPixelRect)) ||
        !allocationSizeIsValid(capacity.batchCount, sizeof(UIDrawBatch)))
    {
        return Core::failure(RenderErrorCode::InvalidDisplayListCapacity,
                             "UI DisplayList capacity exceeds the addressable storage size");
    }

    UIDrawCommand* commands = nullptr;
    UIPixelRect* clips = nullptr;
    UIDrawBatch* batches = nullptr;
    try
    {
        commands = static_cast<UIDrawCommand*>(
            storage.allocate(sizeof(UIDrawCommand) * capacity.commandCount, alignof(UIDrawCommand)));
        if (capacity.clipCount != 0)
        {
            clips = static_cast<UIPixelRect*>(
                storage.allocate(sizeof(UIPixelRect) * capacity.clipCount, alignof(UIPixelRect)));
        }
        batches = static_cast<UIDrawBatch*>(
            storage.allocate(sizeof(UIDrawBatch) * capacity.batchCount, alignof(UIDrawBatch)));
    } catch (const std::bad_alloc&)
    {
        if (batches != nullptr)
        {
            storage.deallocate(batches, sizeof(UIDrawBatch) * capacity.batchCount, alignof(UIDrawBatch));
        }
        if (clips != nullptr)
        {
            storage.deallocate(clips, sizeof(UIPixelRect) * capacity.clipCount, alignof(UIPixelRect));
        }
        if (commands != nullptr)
        {
            storage.deallocate(commands, sizeof(UIDrawCommand) * capacity.commandCount, alignof(UIDrawCommand));
        }
        return Core::failure(RenderErrorCode::DisplayListStorageAllocationFailed,
                             "UI DisplayList fixed storage allocation failed");
    }

    return UIDisplayListBuilder{capacity, storage, commands, clips, batches};
}

UIDisplayListBuilder::UIDisplayListBuilder(UIDisplayListCapacity capacity, std::pmr::memory_resource& storage,
                                           UIDrawCommand* commands, UIPixelRect* clips, UIDrawBatch* batches) noexcept
    : m_capacity(capacity), m_storage(&storage), m_commands(commands), m_clips(clips), m_batches(batches)
{
}

UIDisplayListBuilder::UIDisplayListBuilder(UIDisplayListBuilder&& other) noexcept
    : m_capacity(other.m_capacity), m_storage(std::exchange(other.m_storage, nullptr)),
      m_commands(std::exchange(other.m_commands, nullptr)), m_clips(std::exchange(other.m_clips, nullptr)),
      m_batches(std::exchange(other.m_batches, nullptr)), m_commandCount(std::exchange(other.m_commandCount, 0)),
      m_clipCount(std::exchange(other.m_clipCount, 0)), m_batchCount(std::exchange(other.m_batchCount, 0)),
      m_candidateStatistics(other.m_candidateStatistics), m_publishedStatistics(other.m_publishedStatistics),
      m_statistics(other.m_statistics), m_stickyBuildError(other.m_stickyBuildError),
      m_lastPaintOrdinal(other.m_lastPaintOrdinal), m_state(other.m_state)
{
    other.m_capacity = {};
    other.m_candidateStatistics = {};
    other.m_publishedStatistics = {};
    other.m_statistics = {};
    other.m_stickyBuildError.reset();
    other.m_lastPaintOrdinal.reset();
    other.m_state = State::Ready;
}

UIDisplayListBuilder::~UIDisplayListBuilder()
{
    releaseStorage();
}

Core::Status UIDisplayListBuilder::beginFrame()
{
    if (m_state == State::Building)
    {
        return displayListFailure(RenderErrorCode::DisplayListBuildAlreadyOpen,
                                  "A UI DisplayList build is already open");
    }

    clearCandidate();
    m_publishedStatistics = {};
    m_stickyBuildError.reset();
    m_state = State::Building;
    ++m_statistics.begunBuildCount;
    return Core::success();
}

Core::Status UIDisplayListBuilder::addSolidQuad(const UISolidQuadInput& input)
{
    if (m_state != State::Building)
    {
        return displayListFailure(RenderErrorCode::DisplayListBuildNotOpen,
                                  "A UI DisplayList build must be open before adding commands");
    }
    if (m_stickyBuildError.has_value())
    {
        return displayListFailure(*m_stickyBuildError, "The UI DisplayList build has already failed");
    }
    if (m_lastPaintOrdinal.has_value() && input.paintOrdinal <= *m_lastPaintOrdinal)
    {
        ++m_statistics.invalidInputFailureCount;
        return failBuild(RenderErrorCode::InvalidDrawCommand,
                         "UI draw command paint ordinals must be strictly increasing");
    }
    m_lastPaintOrdinal = input.paintOrdinal;
    if (!input.color.isValid())
    {
        ++m_statistics.invalidInputFailureCount;
        return failBuild(RenderErrorCode::InvalidPremultipliedColor, "UI colors must use premultiplied RGBA8 channels");
    }
    const float maximumCornerRadius =
        static_cast<float>((std::min)(input.bounds.width, input.bounds.height)) * 0.5F;
    if (!validCornerRadii(input.cornerRadii, maximumCornerRadius))
    {
        ++m_statistics.invalidInputFailureCount;
        return failBuild(RenderErrorCode::InvalidDrawCommand,
                         "UI solid quad corner radii must be finite and fit within its bounds");
    }
    if (input.vertices.has_value() &&
        (!input.cornerRadii.empty() ||
         !validSolidQuadVertices(*input.vertices, input.bounds)))
    {
        ++m_statistics.invalidInputFailureCount;
        return failBuild(
            RenderErrorCode::InvalidDrawCommand,
            "UI solid quad vertices must be finite, strictly convex, covered by bounds, and cannot be combined with corner radii");
    }
    if (input.bounds.empty())
    {
        ++m_candidateStatistics.prunedEmptyBoundsCount;
        return Core::success();
    }
    if (input.color.transparent())
    {
        ++m_candidateStatistics.prunedTransparentCount;
        return Core::success();
    }
    if (input.effectiveClip.has_value() && input.effectiveClip->empty())
    {
        ++m_candidateStatistics.prunedEmptyClipCount;
        return Core::success();
    }
    if (input.effectiveClip.has_value() && !intersects(input.bounds, *input.effectiveClip))
    {
        ++m_candidateStatistics.prunedOutsideClipCount;
        return Core::success();
    }

    UIClipId clip{};
    bool needsClip = false;
    if (input.effectiveClip.has_value())
    {
        clip = findClip(*input.effectiveClip);
        needsClip = !clip.hasClip();
        if (needsClip)
        {
            clip = UIClipId{m_clipCount + 1U};
        }
    }

    const bool needsBatch = m_batchCount == 0 || m_batches[m_batchCount - 1U].kind != UIDrawCommandKind::SolidQuad ||
                            m_batches[m_batchCount - 1U].clip != clip;
    if (!hasCapacityFor(needsClip, needsBatch))
    {
        ++m_statistics.capacityFailureCount;
        return failBuild(RenderErrorCode::DisplayListCapacityExceeded,
                         "UI DisplayList fixed command, clip, or batch capacity was exceeded");
    }

    if (needsClip)
    {
        std::construct_at(&m_clips[m_clipCount], *input.effectiveClip);
        ++m_clipCount;
    }

    const u32 commandIndex = m_commandCount;
    std::construct_at(&m_commands[m_commandCount], UIDrawCommand{
                                                       .kind = UIDrawCommandKind::SolidQuad,
                                                       .paintOrdinal = input.paintOrdinal,
                                                       .bounds = input.bounds,
                                                       .color = input.color,
                                                       .cornerRadii = input.cornerRadii,
                                                       .vertices = input.vertices,
                                                       .clip = clip,
                                                   });
    ++m_commandCount;

    if (needsBatch)
    {
        std::construct_at(&m_batches[m_batchCount], UIDrawBatch{
                                                        .kind = UIDrawCommandKind::SolidQuad,
                                                        .clip = clip,
                                                        .firstCommand = commandIndex,
                                                        .commandCount = 1,
                                                    });
        ++m_batchCount;
    } else
    {
        ++m_batches[m_batchCount - 1U].commandCount;
    }

    ++m_candidateStatistics.solidQuadCommandCount;
    return Core::success();
}

Core::Status UIDisplayListBuilder::addSolidEllipse(const UISolidEllipseInput& input)
{
    if (m_state != State::Building)
    {
        return displayListFailure(RenderErrorCode::DisplayListBuildNotOpen,
                                  "A UI DisplayList build must be open before adding commands");
    }
    if (m_stickyBuildError.has_value())
    {
        return displayListFailure(*m_stickyBuildError, "The UI DisplayList build has already failed");
    }
    if (m_lastPaintOrdinal.has_value() && input.paintOrdinal <= *m_lastPaintOrdinal)
    {
        ++m_statistics.invalidInputFailureCount;
        return failBuild(RenderErrorCode::InvalidDrawCommand,
                         "UI draw command paint ordinals must be strictly increasing");
    }
    m_lastPaintOrdinal = input.paintOrdinal;
    if (!input.color.isValid())
    {
        ++m_statistics.invalidInputFailureCount;
        return failBuild(RenderErrorCode::InvalidPremultipliedColor,
                         "UI colors must use premultiplied RGBA8 channels");
    }
    const float maximumStrokeWidth =
        static_cast<float>((std::min)(input.bounds.width, input.bounds.height)) * 0.5F;
    if (!std::isfinite(input.strokeWidth) || input.strokeWidth < 0.0F ||
        input.strokeWidth > maximumStrokeWidth)
    {
        ++m_statistics.invalidInputFailureCount;
        return failBuild(RenderErrorCode::InvalidDrawCommand,
                         "UI solid ellipse stroke width must be finite and fit within its bounds");
    }
    if (input.bounds.empty())
    {
        ++m_candidateStatistics.prunedEmptyBoundsCount;
        return Core::success();
    }
    if (input.color.transparent())
    {
        ++m_candidateStatistics.prunedTransparentCount;
        return Core::success();
    }
    if (input.effectiveClip.has_value() && input.effectiveClip->empty())
    {
        ++m_candidateStatistics.prunedEmptyClipCount;
        return Core::success();
    }
    if (input.effectiveClip.has_value() && !intersects(input.bounds, *input.effectiveClip))
    {
        ++m_candidateStatistics.prunedOutsideClipCount;
        return Core::success();
    }

    UIClipId clip{};
    bool needsClip = false;
    if (input.effectiveClip.has_value())
    {
        clip = findClip(*input.effectiveClip);
        needsClip = !clip.hasClip();
        if (needsClip)
        {
            clip = UIClipId{m_clipCount + 1U};
        }
    }

    const bool needsBatch =
        m_batchCount == 0 ||
        m_batches[m_batchCount - 1U].kind != UIDrawCommandKind::SolidEllipse ||
        m_batches[m_batchCount - 1U].clip != clip;
    if (!hasCapacityFor(needsClip, needsBatch))
    {
        ++m_statistics.capacityFailureCount;
        return failBuild(RenderErrorCode::DisplayListCapacityExceeded,
                         "UI DisplayList fixed command, clip, or batch capacity was exceeded");
    }

    if (needsClip)
    {
        std::construct_at(&m_clips[m_clipCount], *input.effectiveClip);
        ++m_clipCount;
    }

    const u32 commandIndex = m_commandCount;
    std::construct_at(&m_commands[m_commandCount], UIDrawCommand{
                                                       .kind = UIDrawCommandKind::SolidEllipse,
                                                       .paintOrdinal = input.paintOrdinal,
                                                       .bounds = input.bounds,
                                                       .color = input.color,
                                                       .strokeWidth = input.strokeWidth,
                                                       .clip = clip,
                                                   });
    ++m_commandCount;

    if (needsBatch)
    {
        std::construct_at(&m_batches[m_batchCount], UIDrawBatch{
                                                        .kind = UIDrawCommandKind::SolidEllipse,
                                                        .clip = clip,
                                                        .firstCommand = commandIndex,
                                                        .commandCount = 1,
                                                    });
        ++m_batchCount;
    }
    else
    {
        ++m_batches[m_batchCount - 1U].commandCount;
    }

    ++m_candidateStatistics.solidEllipseCommandCount;
    return Core::success();
}

Core::Status UIDisplayListBuilder::addGlyphQuad(const UIGlyphQuadInput& input)
{
    if (m_state != State::Building)
    {
        return displayListFailure(RenderErrorCode::DisplayListBuildNotOpen,
                                  "A UI DisplayList build must be open before adding commands");
    }
    if (m_stickyBuildError.has_value())
    {
        return displayListFailure(*m_stickyBuildError, "The UI DisplayList build has already failed");
    }
    if (m_lastPaintOrdinal.has_value() && input.paintOrdinal <= *m_lastPaintOrdinal)
    {
        ++m_statistics.invalidInputFailureCount;
        return failBuild(RenderErrorCode::InvalidDrawCommand,
                         "UI draw command paint ordinals must be strictly increasing");
    }
    m_lastPaintOrdinal = input.paintOrdinal;
    if (!input.color.isValid())
    {
        ++m_statistics.invalidInputFailureCount;
        return failBuild(RenderErrorCode::InvalidPremultipliedColor, "UI colors must use premultiplied RGBA8 channels");
    }
    if (input.atlasUv.empty() && (input.bounds.width != 0 || input.bounds.height != 0))
    {
        // Zero-sized atlas UV is only valid for advance-only / empty glyphs that
        // also have empty screen bounds.
        ++m_statistics.invalidInputFailureCount;
        return failBuild(RenderErrorCode::InvalidDrawCommand,
                         "UI glyph commands with non-empty bounds require non-empty atlas UV");
    }
    if (input.bounds.empty())
    {
        ++m_candidateStatistics.prunedEmptyBoundsCount;
        return Core::success();
    }
    if (input.color.transparent())
    {
        ++m_candidateStatistics.prunedTransparentCount;
        return Core::success();
    }
    if (input.effectiveClip.has_value() && input.effectiveClip->empty())
    {
        ++m_candidateStatistics.prunedEmptyClipCount;
        return Core::success();
    }
    if (input.effectiveClip.has_value() && !intersects(input.bounds, *input.effectiveClip))
    {
        ++m_candidateStatistics.prunedOutsideClipCount;
        return Core::success();
    }

    UIClipId clip{};
    bool needsClip = false;
    if (input.effectiveClip.has_value())
    {
        clip = findClip(*input.effectiveClip);
        needsClip = !clip.hasClip();
        if (needsClip)
        {
            clip = UIClipId{m_clipCount + 1U};
        }
    }

    const bool needsBatch =
        m_batchCount == 0 || m_batches[m_batchCount - 1U].kind != UIDrawCommandKind::Glyph
        || m_batches[m_batchCount - 1U].clip != clip
        || m_batches[m_batchCount - 1U].atlasPage != input.atlasPage;
    if (!hasCapacityFor(needsClip, needsBatch))
    {
        ++m_statistics.capacityFailureCount;
        return failBuild(RenderErrorCode::DisplayListCapacityExceeded,
                         "UI DisplayList fixed command, clip, or batch capacity was exceeded");
    }

    if (needsClip)
    {
        std::construct_at(&m_clips[m_clipCount], *input.effectiveClip);
        ++m_clipCount;
    }

    const u32 commandIndex = m_commandCount;
    std::construct_at(&m_commands[m_commandCount], UIDrawCommand{
                                                       .kind = UIDrawCommandKind::Glyph,
                                                       .paintOrdinal = input.paintOrdinal,
                                                       .bounds = input.bounds,
                                                       .color = input.color,
                                                       .clip = clip,
                                                       .atlasUv = input.atlasUv,
                                                       .atlasPage = input.atlasPage,
                                                   });
    ++m_commandCount;

    if (needsBatch)
    {
        std::construct_at(&m_batches[m_batchCount], UIDrawBatch{
                                                        .kind = UIDrawCommandKind::Glyph,
                                                        .clip = clip,
                                                        .atlasPage = input.atlasPage,
                                                        .firstCommand = commandIndex,
                                                        .commandCount = 1,
                                                    });
        ++m_batchCount;
    } else
    {
        ++m_batches[m_batchCount - 1U].commandCount;
    }

    ++m_candidateStatistics.glyphCommandCount;
    return Core::success();
}

Core::Status UIDisplayListBuilder::addImageQuad(const UIImageQuadInput& input)
{
    if (m_state != State::Building)
    {
        return displayListFailure(RenderErrorCode::DisplayListBuildNotOpen,
                                  "A UI DisplayList build must be open before adding commands");
    }
    if (m_stickyBuildError.has_value())
    {
        return displayListFailure(*m_stickyBuildError, "The UI DisplayList build has already failed");
    }
    if (m_lastPaintOrdinal.has_value() && input.paintOrdinal <= *m_lastPaintOrdinal)
    {
        ++m_statistics.invalidInputFailureCount;
        return failBuild(RenderErrorCode::InvalidDrawCommand,
                         "UI draw command paint ordinals must be strictly increasing");
    }
    m_lastPaintOrdinal = input.paintOrdinal;
    const bool validUv = std::isfinite(input.uv.u0) && std::isfinite(input.uv.v0) &&
                         std::isfinite(input.uv.u1) && std::isfinite(input.uv.v1) &&
                         input.uv.u0 >= 0.0F && input.uv.v0 >= 0.0F &&
                         input.uv.u1 <= 1.0F && input.uv.v1 <= 1.0F &&
                         input.uv.u0 < input.uv.u1 && input.uv.v0 < input.uv.v1;
    const bool validSampling = input.sampling == UITextureSampling::Linear ||
                               input.sampling == UITextureSampling::Nearest;
    if (!input.color.isValid() || !input.texture.hasValue() || !validUv || !validSampling)
    {
        ++m_statistics.invalidInputFailureCount;
        return failBuild(RenderErrorCode::InvalidDrawCommand,
                         "UI image commands require a valid texture, premultiplied color, UV, and sampling mode");
    }
    if (input.bounds.empty())
    {
        ++m_candidateStatistics.prunedEmptyBoundsCount;
        return Core::success();
    }
    if (input.color.transparent())
    {
        ++m_candidateStatistics.prunedTransparentCount;
        return Core::success();
    }
    if (input.effectiveClip.has_value() && input.effectiveClip->empty())
    {
        ++m_candidateStatistics.prunedEmptyClipCount;
        return Core::success();
    }
    if (input.effectiveClip.has_value() && !intersects(input.bounds, *input.effectiveClip))
    {
        ++m_candidateStatistics.prunedOutsideClipCount;
        return Core::success();
    }

    UIClipId clip{};
    bool needsClip = false;
    if (input.effectiveClip.has_value())
    {
        clip = findClip(*input.effectiveClip);
        needsClip = !clip.hasClip();
        if (needsClip)
        {
            clip = UIClipId{m_clipCount + 1U};
        }
    }
    const bool needsBatch =
        m_batchCount == 0 || m_batches[m_batchCount - 1U].kind != UIDrawCommandKind::ImageQuad ||
        m_batches[m_batchCount - 1U].clip != clip ||
        m_batches[m_batchCount - 1U].texture != input.texture ||
        m_batches[m_batchCount - 1U].sampling != input.sampling;
    if (!hasCapacityFor(needsClip, needsBatch))
    {
        ++m_statistics.capacityFailureCount;
        return failBuild(RenderErrorCode::DisplayListCapacityExceeded,
                         "UI DisplayList fixed command, clip, or batch capacity was exceeded");
    }
    if (needsClip)
    {
        std::construct_at(&m_clips[m_clipCount], *input.effectiveClip);
        ++m_clipCount;
    }

    const u32 commandIndex = m_commandCount;
    std::construct_at(&m_commands[m_commandCount], UIDrawCommand{
                                                       .kind = UIDrawCommandKind::ImageQuad,
                                                       .paintOrdinal = input.paintOrdinal,
                                                       .bounds = input.bounds,
                                                       .color = input.color,
                                                       .clip = clip,
                                                       .texture = input.texture,
                                                       .resourceOrdinal = input.resourceOrdinal,
                                                       .uv = input.uv,
                                                       .sampling = input.sampling,
                                                   });
    ++m_commandCount;
    if (needsBatch)
    {
        std::construct_at(&m_batches[m_batchCount], UIDrawBatch{
                                                        .kind = UIDrawCommandKind::ImageQuad,
                                                        .clip = clip,
                                                        .texture = input.texture,
                                                        .sampling = input.sampling,
                                                        .firstCommand = commandIndex,
                                                        .commandCount = 1,
                                                    });
        ++m_batchCount;
    }
    else
    {
        ++m_batches[m_batchCount - 1U].commandCount;
    }
    ++m_candidateStatistics.imageQuadCommandCount;
    return Core::success();
}

Core::Result<UIDisplayListView> UIDisplayListBuilder::commit()
{
    if (m_state != State::Building)
    {
        return Core::failure(RenderErrorCode::DisplayListBuildNotOpen,
                             "A UI DisplayList build must be open before commit");
    }
    if (m_stickyBuildError.has_value())
    {
        const Core::ErrorCode code = *m_stickyBuildError;
        rollbackBuilding();
        return Core::failure(code, "The UI DisplayList build failed and was rolled back");
    }

    m_candidateStatistics.clipCount = m_clipCount;
    m_candidateStatistics.batchCount = m_batchCount;
    m_candidateStatistics.paintOrderChecksum = calculatePaintOrderChecksum(
        std::span<const UIDrawCommand>{m_commands, m_commandCount}, std::span<const UIPixelRect>{m_clips, m_clipCount});
    m_publishedStatistics = m_candidateStatistics;
    m_state = State::Published;
    ++m_statistics.committedBuildCount;
    return publishedView();
}

void UIDisplayListBuilder::rollback() noexcept
{
    if (m_state == State::Building)
    {
        rollbackBuilding();
    }
}

UIDisplayListView UIDisplayListBuilder::publishedView() const noexcept
{
    if (m_state != State::Published)
    {
        return {};
    }
    return UIDisplayListView{std::span<const UIDrawCommand>{m_commands, m_commandCount},
                             std::span<const UIPixelRect>{m_clips, m_clipCount},
                             std::span<const UIDrawBatch>{m_batches, m_batchCount}, m_publishedStatistics};
}

UIDisplayListCapacity UIDisplayListBuilder::capacity() const noexcept
{
    return m_capacity;
}

UIDisplayListBuilderStatistics UIDisplayListBuilder::statistics() const noexcept
{
    return m_statistics;
}

Core::Status UIDisplayListBuilder::failBuild(Core::ErrorCode code, const char* message)
{
    if (!m_stickyBuildError.has_value())
    {
        m_stickyBuildError = code;
    }
    return displayListFailure(*m_stickyBuildError, message);
}

u64 UIDisplayListBuilder::calculatePaintOrderChecksum(std::span<const UIDrawCommand> commands,
                                                      std::span<const UIPixelRect> clips) noexcept
{
    u64 checksum = Fnv1aOffsetBasis;
    for (const UIDrawCommand& command : commands)
    {
        hashU32(checksum, command.paintOrdinal);
        hashByte(checksum, static_cast<u8>(command.kind));
        hashU32(checksum, static_cast<u32>(command.bounds.x));
        hashU32(checksum, static_cast<u32>(command.bounds.y));
        hashU32(checksum, command.bounds.width);
        hashU32(checksum, command.bounds.height);
        hashByte(checksum, command.color.red);
        hashByte(checksum, command.color.green);
        hashByte(checksum, command.color.blue);
        hashByte(checksum, command.color.alpha);
        hashU32(checksum, std::bit_cast<u32>(command.cornerRadii.topLeft));
        hashU32(checksum, std::bit_cast<u32>(command.cornerRadii.topRight));
        hashU32(checksum, std::bit_cast<u32>(command.cornerRadii.bottomRight));
        hashU32(checksum, std::bit_cast<u32>(command.cornerRadii.bottomLeft));
        hashByte(checksum, static_cast<u8>(command.vertices.has_value()));
        if (command.vertices.has_value())
        {
            for (const UISubpixelPoint point : quadPoints(*command.vertices))
            {
                hashU32(checksum, std::bit_cast<u32>(point.x));
                hashU32(checksum, std::bit_cast<u32>(point.y));
            }
        }
        hashU32(checksum, std::bit_cast<u32>(command.strokeWidth));
        hashByte(checksum, static_cast<u8>(command.clip.hasClip()));
        hashU32(checksum, command.clip.m_value);
        if (command.clip.hasClip())
        {
            const UIPixelRect& clip = clips[command.clip.m_value - 1U];
            hashU32(checksum, static_cast<u32>(clip.x));
            hashU32(checksum, static_cast<u32>(clip.y));
            hashU32(checksum, clip.width);
            hashU32(checksum, clip.height);
        }
        if (command.kind == UIDrawCommandKind::Glyph)
        {
            hashU32(checksum, static_cast<u32>(command.atlasUv.x));
            hashU32(checksum, static_cast<u32>(command.atlasUv.y));
            hashU32(checksum, command.atlasUv.width);
            hashU32(checksum, command.atlasUv.height);
            hashU32(checksum, command.atlasPage);
        }
        else if (command.kind == UIDrawCommandKind::ImageQuad)
        {
            hashU32(checksum, command.resourceOrdinal);
            hashU32(checksum, std::bit_cast<u32>(command.uv.u0));
            hashU32(checksum, std::bit_cast<u32>(command.uv.v0));
            hashU32(checksum, std::bit_cast<u32>(command.uv.u1));
            hashU32(checksum, std::bit_cast<u32>(command.uv.v1));
            hashByte(checksum, static_cast<u8>(command.sampling));
        }
    }
    return checksum;
}

UIClipId UIDisplayListBuilder::findClip(const UIPixelRect& clip) const noexcept
{
    if (m_clipCount == 0)
    {
        return {};
    }
    const UIPixelRect* const begin = m_clips;
    const UIPixelRect* const end = begin + m_clipCount;
    const UIPixelRect* const found = std::find(begin, end, clip);
    if (found == end)
    {
        return {};
    }
    return UIClipId{static_cast<u32>(found - begin) + 1U};
}

bool UIDisplayListBuilder::hasCapacityFor(bool needsClip, bool needsBatch) const noexcept
{
    if (m_commandCount >= m_capacity.commandCount)
    {
        return false;
    }
    if (needsClip && m_clipCount >= m_capacity.clipCount)
    {
        return false;
    }
    return !needsBatch || m_batchCount < m_capacity.batchCount;
}

void UIDisplayListBuilder::clearCandidate() noexcept
{
    if (m_commandCount != 0)
    {
        std::destroy_n(m_commands, m_commandCount);
    }
    if (m_clipCount != 0)
    {
        std::destroy_n(m_clips, m_clipCount);
    }
    if (m_batchCount != 0)
    {
        std::destroy_n(m_batches, m_batchCount);
    }
    m_commandCount = 0;
    m_clipCount = 0;
    m_batchCount = 0;
    m_candidateStatistics = {};
    m_lastPaintOrdinal.reset();
}

void UIDisplayListBuilder::releaseStorage() noexcept
{
    if (m_storage == nullptr)
    {
        return;
    }
    clearCandidate();
    if (m_batches != nullptr)
    {
        m_storage->deallocate(m_batches, sizeof(UIDrawBatch) * m_capacity.batchCount, alignof(UIDrawBatch));
    }
    if (m_clips != nullptr)
    {
        m_storage->deallocate(m_clips, sizeof(UIPixelRect) * m_capacity.clipCount, alignof(UIPixelRect));
    }
    if (m_commands != nullptr)
    {
        m_storage->deallocate(m_commands, sizeof(UIDrawCommand) * m_capacity.commandCount, alignof(UIDrawCommand));
    }
    m_storage = nullptr;
    m_commands = nullptr;
    m_clips = nullptr;
    m_batches = nullptr;
}

void UIDisplayListBuilder::rollbackBuilding() noexcept
{
    clearCandidate();
    m_publishedStatistics = {};
    m_stickyBuildError.reset();
    m_state = State::Ready;
    ++m_statistics.rolledBackBuildCount;
}

} // namespace Tina::Render
