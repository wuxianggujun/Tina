#include <tina/ui/UIVirtualStick.hpp>

// Travel keeps the knob inside the base, so it is the difference of the radii
// rather than the base radius.
static_assert(Tina::UI::virtualStickTravelRadius(Tina::UI::UIVirtualStickConfig{}) == 32.0F);
static_assert(Tina::UI::isValidVirtualStickConfig(Tina::UI::UIVirtualStickConfig{}));
// A knob at least as large as its base has nowhere to travel.
static_assert(!Tina::UI::isValidVirtualStickConfig(
    Tina::UI::UIVirtualStickConfig{.baseRadius = 24.0F, .knobRadius = 24.0F}));
static_assert(!Tina::UI::UIVirtualStickState{}.engaged);
