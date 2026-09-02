#pragma once

#include <tina/asset_format/SourceImportMetadataFormat.hpp>
#include <tina/core/base/Types.hpp>

namespace Tina::EditorApp::Detail {

inline constexpr Core::u32 EditorSourceImportPathByteCapacity =
    AssetFormat::SourceImportWire::MaxPathBytes;
inline constexpr Core::u32 EditorSourceImportUnitCapacity = 4096;

static_assert(EditorSourceImportUnitCapacity <= AssetFormat::SourceImportWire::MaxUnits);

} // namespace Tina::EditorApp::Detail
