#include <tina/ui/UIDirty.hpp>

#include <type_traits>

static_assert(sizeof(Tina::UI::UIDirty) == sizeof(Tina::u16));
static_assert(std::is_enum_v<Tina::UI::UIDirty>);
static_assert(Tina::UI::dirtyMaskValue(Tina::UI::UIDirty::Structure) == 1U);
static_assert(Tina::UI::hasDirty(
    Tina::UI::UIDirty::Structure | Tina::UI::UIDirty::Paint,
    Tina::UI::UIDirty::Paint));
static_assert(!Tina::UI::hasDirty(Tina::UI::UIDirty::Structure, Tina::UI::UIDirty::Paint));
static_assert(Tina::UI::clearDirty(
                  Tina::UI::UIDirty::Structure | Tina::UI::UIDirty::Paint,
                  Tina::UI::UIDirty::Structure)
    == Tina::UI::UIDirty::Paint);
