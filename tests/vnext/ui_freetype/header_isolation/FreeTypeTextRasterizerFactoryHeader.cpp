#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>

// Factory header must compile without FreeType tokens in the TU.
static_assert(sizeof(Tina::UI::UITextRasterizerCapacity) > 0);
