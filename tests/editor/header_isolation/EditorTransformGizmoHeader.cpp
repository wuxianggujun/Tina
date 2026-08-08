#include <tina/editor/EditorTransformGizmo.hpp>

#include <type_traits>

static_assert(Tina::Editor::EditorTransformGizmoHandleCapacity == 7U);
static_assert(std::is_trivially_copyable_v<
              Tina::Editor::EditorTransformGizmoSnapshot>);
static_assert(Tina::Editor::EditorTransformGizmoDelta{}.scaleFactors.x == 1.0F);
