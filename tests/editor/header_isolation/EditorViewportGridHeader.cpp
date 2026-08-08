#include <tina/editor/EditorViewportGrid.hpp>

static_assert(Tina::Editor::EditorViewportGridSegmentCapacity >= 96U);
static_assert(Tina::Editor::EditorViewportGridConfig{}.zoomPercent == 100.0F);
