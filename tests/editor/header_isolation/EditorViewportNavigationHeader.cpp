#include <tina/editor/EditorViewportNavigation.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<
              Tina::Editor::EditorViewportNavigationSnapshot>);
static_assert(Tina::Editor::EditorViewportNavigationLimits::MaximumInputCommandsPerBatch ==
              64U);
