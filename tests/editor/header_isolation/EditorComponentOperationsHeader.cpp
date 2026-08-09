#include <tina/editor/EditorComponentOperations.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<
              Tina::Editor::EditorComponentInfo>);
