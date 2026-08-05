#include <tina/asset/CatalogPackageWatcher.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Asset::CatalogPackageWatcher>);
static_assert(std::is_nothrow_move_constructible_v<Tina::Asset::CatalogPackageWatcher>);

[[maybe_unused]] constexpr Tina::Asset::CatalogPackageWatchState State =
    Tina::Asset::CatalogPackageWatchState::Quiet;
