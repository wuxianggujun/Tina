#include <tina/save/SaveMigration.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Save::SaveMigrationPipeline>);
static_assert(std::is_move_constructible_v<Tina::Save::SaveMigrationPipeline>);
