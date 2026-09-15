#include <tina/asset_format/World2DSnapshot.hpp>

static_assert(Tina::AssetFormat::World2DSnapshotWire::SchemaVersion == 9U);
static_assert(Tina::AssetFormat::World2DSnapshotWire::EntityBytes == 512U);
static_assert(Tina::AssetFormat::World2DNodeKindCount == 17U);
static_assert(static_cast<Tina::Core::u16>(Tina::AssetFormat::World2DNodeKind::PrefabInstance2D) ==
              16U);
