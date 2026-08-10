#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/UIDisplayList.hpp>
#include <tina/render/Texture2DFrameResourceResolver.hpp>
#include <tina/ui/UICommittedPaint.hpp>

namespace Tina::Integration {

// Framebuffer destination for one committed logical UI snapshot. The mapping
// deliberately carries no windowing content-scale state: the actual logical
// and framebuffer extents are the authoritative scale pair.
struct UIRenderViewportMapping final {
    Render::UIPixelRect framebufferViewport{};
};

struct UIRenderDisplayListBuildStatistics final {
    usize sourcePaintEntryCount = 0;
    usize submittedSolidQuadCount = 0;
    usize submittedSolidEllipseCount = 0;
    usize submittedSolidLineCount = 0;
    usize submittedGlyphCount = 0;
    usize submittedImageQuadCount = 0;
    usize resolvedImageResourceCount = 0;
    usize skippedImageMissingResolverCount = 0;
    usize skippedImageUnavailableCount = 0;
    usize skippedImageExtentMismatchCount = 0;
    usize redundantClipElisionCount = 0;
};

enum class UIRenderImageResolutionState : u8 {
    Empty = 0,
    MissingResolver,
    Unavailable,
    Ready,
};

struct UIRenderImageResolutionCacheEntry final {
    UI::UINodeId root{};
    Core::AssetId asset{};
    Render::Texture2DFrameResourceResolution resolution{};
    u32 resourceOrdinal = 0;
    UIRenderImageResolutionState state = UIRenderImageResolutionState::Empty;
};

struct UIRenderImageResolverLookup final {
    using FindFn = const Render::Texture2DFrameResourceResolver* (*)(
        const void* userData, UI::UINodeId root) noexcept;

    const void* userData = nullptr;
    FindFn find = nullptr;
};

struct UIRenderImageBuildContext final {
    Render::FrameResourceSink* resourceSink = nullptr;
    UIRenderImageResolverLookup resolverLookup{};
    // Open-addressed fixed scratch. The caller owns and reuses this storage.
    std::span<UIRenderImageResolutionCacheEntry> cache{};
};

// The DisplayList view borrows the builder's fixed storage and follows its
// single-buffer invalidation contract. A subsequent beginFrame() invalidates
// it immediately, including when that replacement is later rolled back.
// Moving or destroying the builder also invalidates it; rollback() by itself
// is a no-op when no build is open.
struct UIRenderDisplayListBuild final {
    Render::UIDisplayListView displayList{};
    UIRenderDisplayListBuildStatistics statistics{};
};

// Owns one complete builder transaction. If beginFrame() fails, an already-open
// caller transaction is left untouched. Once beginFrame() succeeds, every
// validation, conversion, or capacity failure rolls the new build back.
[[nodiscard]] Core::Result<UIRenderDisplayListBuild> buildUIDisplayList(
    Render::UIDisplayListBuilder& builder,
    UI::UICommittedPaintView paintView,
    UIRenderViewportMapping mapping,
    UIRenderImageBuildContext imageContext = {});

} // namespace Tina::Integration
