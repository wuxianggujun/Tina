#pragma once

#include <tina/ui/UIPaint.hpp>
#include <optional>

namespace Tina::UI {

struct UINineSlice final {
    UIImagePixelInsets sourceInsets{};
    UIEdgeSpacing destinationInsets{};
    auto operator<=>(const UINineSlice&) const = default;
};

// Composable background owner, NOT a widget base class. It owns the texture's
// retained identity and slice metadata, never a native/GPU texture. Runtime's
// existing AssetId resolver and frame pin own GPU lifetime. Descriptor creation
// copies this value into the same bounded Canvas pool used by other paint.
class UIPanel final {
  public:
    UIImageSource texture{};
    std::optional<UINineSlice> nineSlice{};
    UIStraightSrgba8Color tint = rgba8(255, 255, 255);
    UIImageSampling sampling = UIImageSampling::Linear;

    [[nodiscard]] constexpr UICanvasCommand backgroundCommand() const noexcept
    {
        return UICanvasCommand{
            .kind = nineSlice ? UICanvasCommandKind::NineSlice : UICanvasCommandKind::Image,
            .bounds = {}, .boundsMode = UICanvasBoundsMode::ElementBorderBox,
            .color = tint, .imageSource = texture,
            .imageSourceInsets = nineSlice ? nineSlice->sourceInsets : UIImagePixelInsets{},
            .imageDestinationInsets = nineSlice ? nineSlice->destinationInsets : UIEdgeSpacing{},
            .imageSampling = sampling,
        };
    }
};

} // namespace Tina::UI
