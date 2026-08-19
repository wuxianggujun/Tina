#include <tina/ui/UIDivider.hpp>

static_assert(Tina::UI::UIDividerConfig{}.orientation ==
              Tina::UI::UIDividerOrientation::Horizontal);
static_assert(Tina::UI::UIDividerConfig{}.thickness == 1.0F);
