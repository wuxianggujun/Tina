#include <tina/asset_format/LocalizationTablePayload.hpp>

static_assert(Tina::AssetFormat::LocalizationTableWire::HeaderBytes == 56U);
static_assert(Tina::AssetFormat::LocalizationTableWire::EntryBytes == 16U);
static_assert(Tina::AssetFormat::LocalizationTableWire::SchemaVersion == 1U);
static_assert(Tina::AssetFormat::LocalizationTableWire::MaximumLocaleTagBytes == 35U);
// The hash must be a constant expression: the runtime interns compile-time keys through it.
static_assert(Tina::AssetFormat::localizationKeyHash("ui.play") !=
              Tina::AssetFormat::localizationKeyHash("ui.quit"));
static_assert(Tina::AssetFormat::localizationKeyHash("") ==
              Tina::AssetFormat::LocalizationTableWire::KeyHashOffsetBasis);
