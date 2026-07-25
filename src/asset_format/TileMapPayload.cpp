#include <tina/asset_format/TileMapPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/core/text/Utf8.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <new>
#include <stdexcept>

namespace Tina::AssetFormat {
namespace {

using Core::u16;
using Core::u32;
using Core::u8;
using Core::usize;

struct ParsedObject final {
    TileMapObjectPayloadView view{};
    usize nextOffset = 0;
};

struct ParsedLayer final {
    TileMapLayerPayloadView view{};
    usize nextOffset = 0;
};

[[nodiscard]] bool hasBytes(std::span<const std::byte> bytes, usize offset, usize count) noexcept
{
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

[[nodiscard]] u8 readU8(std::span<const std::byte> bytes, usize offset) noexcept
{
    return std::to_integer<u8>(bytes[offset]);
}

[[nodiscard]] u16 readU16(std::span<const std::byte> bytes, usize offset) noexcept
{
    return static_cast<u16>(readU8(bytes, offset)) |
           static_cast<u16>(static_cast<u16>(readU8(bytes, offset + 1U)) << 8U);
}

[[nodiscard]] u32 readU32(std::span<const std::byte> bytes, usize offset) noexcept
{
    u32 value = 0;
    for (usize index = 0; index < 4U; ++index)
    {
        value |= static_cast<u32>(readU8(bytes, offset + index)) << (index * 8U);
    }
    return value;
}

[[nodiscard]] float readF32(std::span<const std::byte> bytes, usize offset) noexcept
{
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void appendU8(std::vector<std::byte>& bytes, u8 value)
{
    bytes.push_back(static_cast<std::byte>(value));
}

void appendU16(std::vector<std::byte>& bytes, u16 value)
{
    appendU8(bytes, static_cast<u8>(value & 0xFFU));
    appendU8(bytes, static_cast<u8>((value >> 8U) & 0xFFU));
}

void appendU32(std::vector<std::byte>& bytes, u32 value)
{
    for (usize index = 0; index < 4U; ++index)
    {
        appendU8(bytes, static_cast<u8>((value >> (index * 8U)) & 0xFFU));
    }
}

void appendF32(std::vector<std::byte>& bytes, float value)
{
    const usize offset = bytes.size();
    bytes.resize(offset + sizeof(value));
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void appendText(std::vector<std::byte>& bytes, std::string_view text)
{
    const usize offset = bytes.size();
    bytes.resize(offset + text.size());
    if (!text.empty())
    {
        std::memcpy(bytes.data() + offset, text.data(), text.size());
    }
}

[[nodiscard]] bool isFinite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool isKnownLayerKind(TileMapLayerKind kind) noexcept
{
    return kind == TileMapLayerKind::Tile || kind == TileMapLayerKind::Object;
}

[[nodiscard]] bool isKnownObjectKind(TileMapObjectKind kind) noexcept
{
    return kind == TileMapObjectKind::Point || kind == TileMapObjectKind::Rectangle;
}

[[nodiscard]] Core::Status validateText(std::string_view text, bool requireNonEmpty, const char* field)
{
    if ((requireNonEmpty && text.empty()) || text.size() > TileMapWire::MaxStringBytes ||
        !Core::isStrictUtf8WithoutNul(text))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, field);
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateProperties(std::span<const TileMapPropertyDesc> properties)
{
    if (properties.size() > TileMapWire::MaxPropertiesPerOwner)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLimits, "tilemap property count exceeds limit");
    }
    for (usize index = 0; index < properties.size(); ++index)
    {
        if (const auto status = validateText(properties[index].key, true, "tilemap property key invalid"); !status)
        {
            return status;
        }
        if (const auto status = validateText(properties[index].value, false, "tilemap property value invalid"); !status)
        {
            return status;
        }
        for (usize prior = 0; prior < index; ++prior)
        {
            if (properties[prior].key == properties[index].key)
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout, "duplicate tilemap property key");
            }
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateObject(const TileMapObjectDesc& object)
{
    if (object.stableObjectId == 0 || !isKnownObjectKind(object.kind))
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity, "tilemap object id or kind invalid");
    }
    if (const auto status = validateText(object.name, false, "tilemap object name invalid"); !status)
    {
        return status;
    }
    if (const auto status = validateProperties(object.properties); !status)
    {
        return status;
    }
    if (!isFinite(object.x) || !isFinite(object.y) || !isFinite(object.width) || !isFinite(object.height))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap object geometry must be finite");
    }
    if (object.kind == TileMapObjectKind::Point && (object.width != 0.0f || object.height != 0.0f))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap point object must have zero extent");
    }
    if (object.kind == TileMapObjectKind::Rectangle && (!(object.width > 0.0f) || !(object.height > 0.0f)))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap rectangle extent must be positive");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateDesc(const TileMapPayloadDesc& desc)
{
    if (desc.widthCells == 0 || desc.heightCells == 0 || desc.widthCells > TileMapWire::MaxDimension ||
        desc.heightCells > TileMapWire::MaxDimension)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap dimensions out of range");
    }
    if (!isFinite(desc.cellSizeMeters) || !(desc.cellSizeMeters > 0.0f))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "cellSizeMeters must be positive finite");
    }
    if (desc.layers.empty() || desc.layers.size() > TileMapWire::MaxLayers)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLimits, "tilemap layer count out of range");
    }

    const u32 expectedTileCount = desc.widthCells * desc.heightCells;
    std::array<TileMapObjectId, TileMapWire::MaxObjectsPerMap> objectIds{};
    usize objectIdCount = 0;
    for (usize index = 0; index < desc.layers.size(); ++index)
    {
        const TileMapLayerDesc& layer = desc.layers[index];
        if (layer.stableLayerId == 0 || !isKnownLayerKind(layer.kind))
        {
            return Core::failure(AssetFormatErrorCode::InvalidIdentity, "tilemap layer id or kind invalid");
        }
        for (usize prior = 0; prior < index; ++prior)
        {
            if (desc.layers[prior].stableLayerId == layer.stableLayerId)
            {
                return Core::failure(AssetFormatErrorCode::InvalidIdentity, "duplicate tilemap layer id");
            }
        }
        if (const auto status = validateText(layer.name, false, "tilemap layer name invalid"); !status)
        {
            return status;
        }
        if (const auto status = validateProperties(layer.properties); !status)
        {
            return status;
        }

        if (layer.kind == TileMapLayerKind::Tile)
        {
            if (layer.tiles.size() != expectedTileCount || !layer.objects.empty())
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap tile layer content mismatch");
            }
            continue;
        }

        if (!layer.tiles.empty() || layer.objects.size() > TileMapWire::MaxObjectsPerLayer ||
            layer.objects.size() > TileMapWire::MaxObjectsPerMap - objectIdCount)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLimits, "tilemap object layer count out of range");
        }
        for (const TileMapObjectDesc& object : layer.objects)
        {
            if (const auto status = validateObject(object); !status)
            {
                return status;
            }
            for (usize prior = 0; prior < objectIdCount; ++prior)
            {
                if (objectIds[prior] == object.stableObjectId)
                {
                    return Core::failure(AssetFormatErrorCode::InvalidIdentity, "duplicate tilemap object id");
                }
            }
            objectIds[objectIdCount++] = object.stableObjectId;
        }
    }
    return Core::success();
}

void appendProperties(std::vector<std::byte>& bytes, std::span<const TileMapPropertyDesc> properties)
{
    for (const TileMapPropertyDesc& property : properties)
    {
        appendU16(bytes, static_cast<u16>(property.key.size()));
        appendU16(bytes, static_cast<u16>(property.value.size()));
        appendText(bytes, property.key);
        appendText(bytes, property.value);
    }
}

void appendObject(std::vector<std::byte>& bytes, const TileMapObjectDesc& object)
{
    appendU32(bytes, object.stableObjectId);
    appendU8(bytes, static_cast<u8>(object.kind));
    appendU8(bytes, object.visible ? TileMapWire::ObjectVisible : 0U);
    appendU16(bytes, static_cast<u16>(object.name.size()));
    appendU16(bytes, static_cast<u16>(object.properties.size()));
    appendU16(bytes, 0U);
    appendF32(bytes, object.x);
    appendF32(bytes, object.y);
    appendF32(bytes, object.width);
    appendF32(bytes, object.height);
    appendText(bytes, object.name);
    appendProperties(bytes, object.properties);
}

void appendLayer(std::vector<std::byte>& bytes, const TileMapLayerDesc& layer)
{
    appendU32(bytes, layer.stableLayerId);
    appendU8(bytes, static_cast<u8>(layer.kind));
    appendU8(bytes, layer.visible ? TileMapWire::LayerVisible : 0U);
    appendU16(bytes, static_cast<u16>(layer.name.size()));
    appendU16(bytes, static_cast<u16>(layer.properties.size()));
    appendU16(bytes, 0U);
    appendU32(bytes, static_cast<u32>(layer.kind == TileMapLayerKind::Tile ? layer.tiles.size() : layer.objects.size()));
    appendText(bytes, layer.name);
    appendProperties(bytes, layer.properties);
    if (layer.kind == TileMapLayerKind::Tile)
    {
        for (const u16 tile : layer.tiles)
        {
            appendU16(bytes, tile);
        }
        return;
    }
    for (const TileMapObjectDesc& object : layer.objects)
    {
        appendObject(bytes, object);
    }
}

[[nodiscard]] Core::Result<std::string_view> takeText(std::span<const std::byte> bytes, usize& offset, usize length)
{
    if (!hasBytes(bytes, offset, length))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap string exceeds payload");
    }
    const auto text = std::string_view{reinterpret_cast<const char*>(bytes.data() + offset), length};
    offset += length;
    return text;
}

struct ParsedProperties final {
    std::span<const std::byte> bytes{};
    usize nextOffset = 0;
};

[[nodiscard]] Core::Result<ParsedProperties> parseProperties(std::span<const std::byte> bytes, usize offset,
                                                              u16 propertyCount)
{
    if (propertyCount > TileMapWire::MaxPropertiesPerOwner)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLimits, "tilemap property count exceeds limit");
    }
    const usize start = offset;
    std::array<std::string_view, TileMapWire::MaxPropertiesPerOwner> keys{};
    for (u16 index = 0; index < propertyCount; ++index)
    {
        if (!hasBytes(bytes, offset, 4U))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap property header truncated");
        }
        const u16 keyBytes = readU16(bytes, offset);
        const u16 valueBytes = readU16(bytes, offset + 2U);
        offset += 4U;
        if (keyBytes > TileMapWire::MaxStringBytes || valueBytes > TileMapWire::MaxStringBytes)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLimits, "tilemap property string exceeds limit");
        }
        auto key = takeText(bytes, offset, keyBytes);
        if (!key)
        {
            return Core::failure(std::move(key.error()));
        }
        auto value = takeText(bytes, offset, valueBytes);
        if (!value)
        {
            return Core::failure(std::move(value.error()));
        }
        if (const auto status = validateText(*key, true, "tilemap property key invalid"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (const auto status = validateText(*value, false, "tilemap property value invalid"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        for (u16 prior = 0; prior < index; ++prior)
        {
            if (keys[prior] == *key)
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout, "duplicate tilemap property key");
            }
        }
        keys[index] = *key;
    }
    return ParsedProperties{.bytes = bytes.subspan(start, offset - start), .nextOffset = offset};
}

[[nodiscard]] Core::Result<ParsedObject> parseObjectAt(std::span<const std::byte> bytes, usize offset)
{
    if (!hasBytes(bytes, offset, TileMapWire::ObjectHeaderBytes))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap object header truncated");
    }
    const TileMapObjectId stableObjectId = readU32(bytes, offset);
    const auto kind = static_cast<TileMapObjectKind>(readU8(bytes, offset + 4U));
    const u8 flags = readU8(bytes, offset + 5U);
    const u16 nameBytes = readU16(bytes, offset + 6U);
    const u16 propertyCount = readU16(bytes, offset + 8U);
    const u16 reserved = readU16(bytes, offset + 10U);
    const float x = readF32(bytes, offset + 12U);
    const float y = readF32(bytes, offset + 16U);
    const float width = readF32(bytes, offset + 20U);
    const float height = readF32(bytes, offset + 24U);
    if (stableObjectId == 0 || !isKnownObjectKind(kind) || (flags & ~TileMapWire::ObjectVisible) != 0U ||
        reserved != 0U ||
        nameBytes > TileMapWire::MaxStringBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap object header invalid");
    }
    if (!isFinite(x) || !isFinite(y) || !isFinite(width) || !isFinite(height))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap object geometry must be finite");
    }
    if ((kind == TileMapObjectKind::Point && (width != 0.0f || height != 0.0f)) ||
        (kind == TileMapObjectKind::Rectangle && (!(width > 0.0f) || !(height > 0.0f))))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap object geometry invalid for kind");
    }

    offset += TileMapWire::ObjectHeaderBytes;
    auto name = takeText(bytes, offset, nameBytes);
    if (!name)
    {
        return Core::failure(std::move(name.error()));
    }
    if (const auto status = validateText(*name, false, "tilemap object name invalid"); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto properties = parseProperties(bytes, offset, propertyCount);
    if (!properties)
    {
        return Core::failure(std::move(properties.error()));
    }
    return ParsedObject{
        .view = TileMapObjectPayloadView{
            .stableObjectId = stableObjectId,
            .kind = kind,
            .visible = (flags & TileMapWire::ObjectVisible) != 0U,
            .name = *name,
            .x = x,
            .y = y,
            .width = width,
            .height = height,
            .propertyCount = propertyCount,
            .propertyBytes = properties->bytes,
        },
        .nextOffset = properties->nextOffset,
    };
}

[[nodiscard]] Core::Result<ParsedLayer> parseLayerAt(std::span<const std::byte> bytes, usize offset, u32 widthCells,
                                                      u32 heightCells)
{
    if (!hasBytes(bytes, offset, TileMapWire::LayerHeaderBytes))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap layer header truncated");
    }
    const TileMapLayerId stableLayerId = readU32(bytes, offset);
    const auto kind = static_cast<TileMapLayerKind>(readU8(bytes, offset + 4U));
    const u8 flags = readU8(bytes, offset + 5U);
    const u16 nameBytes = readU16(bytes, offset + 6U);
    const u16 propertyCount = readU16(bytes, offset + 8U);
    const u16 reserved = readU16(bytes, offset + 10U);
    const u32 contentCount = readU32(bytes, offset + 12U);
    if (stableLayerId == 0 || !isKnownLayerKind(kind) || (flags & ~TileMapWire::LayerVisible) != 0U || reserved != 0U ||
        nameBytes > TileMapWire::MaxStringBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap layer header invalid");
    }

    offset += TileMapWire::LayerHeaderBytes;
    auto name = takeText(bytes, offset, nameBytes);
    if (!name)
    {
        return Core::failure(std::move(name.error()));
    }
    if (const auto status = validateText(*name, false, "tilemap layer name invalid"); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto properties = parseProperties(bytes, offset, propertyCount);
    if (!properties)
    {
        return Core::failure(std::move(properties.error()));
    }
    offset = properties->nextOffset;

    TileMapLayerPayloadView view{
        .stableLayerId = stableLayerId,
        .kind = kind,
        .visible = (flags & TileMapWire::LayerVisible) != 0U,
        .name = *name,
        .propertyCount = propertyCount,
        .propertyBytes = properties->bytes,
        .widthCells = widthCells,
        .heightCells = heightCells,
    };
    if (kind == TileMapLayerKind::Tile)
    {
        const u32 expectedTileCount = widthCells * heightCells;
        if (contentCount != expectedTileCount)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap tile layer count mismatch");
        }
        const usize tileBytes = static_cast<usize>(contentCount) * sizeof(u16);
        if (!hasBytes(bytes, offset, tileBytes))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap tile layer truncated");
        }
        view.tileCount = contentCount;
        view.tileBytes = bytes.subspan(offset, tileBytes);
        offset += tileBytes;
    }
    else
    {
        if (contentCount > TileMapWire::MaxObjectsPerLayer)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLimits, "tilemap object layer count exceeds limit");
        }
        const usize objectStart = offset;
        std::array<TileMapObjectId, TileMapWire::MaxObjectsPerLayer> objectIds{};
        for (u32 index = 0; index < contentCount; ++index)
        {
            auto object = parseObjectAt(bytes, offset);
            if (!object)
            {
                return Core::failure(std::move(object.error()));
            }
            for (u32 prior = 0; prior < index; ++prior)
            {
                if (objectIds[prior] == object->view.stableObjectId)
                {
                    return Core::failure(AssetFormatErrorCode::InvalidIdentity, "duplicate tilemap object id in layer");
                }
            }
            objectIds[index] = object->view.stableObjectId;
            offset = object->nextOffset;
        }
        view.objectCount = contentCount;
        view.objectBytes = bytes.subspan(objectStart, offset - objectStart);
    }
    return ParsedLayer{.view = view, .nextOffset = offset};
}

[[nodiscard]] std::optional<TileMapPropertyView> propertyAt(std::span<const std::byte> bytes, u16 propertyCount,
                                                             u16 index) noexcept
{
    if (index >= propertyCount)
    {
        return std::nullopt;
    }
    usize offset = 0;
    for (u16 current = 0; current < propertyCount; ++current)
    {
        if (!hasBytes(bytes, offset, 4U))
        {
            return std::nullopt;
        }
        const u16 keyBytes = readU16(bytes, offset);
        const u16 valueBytes = readU16(bytes, offset + 2U);
        offset += 4U;
        if (!hasBytes(bytes, offset, static_cast<usize>(keyBytes) + valueBytes))
        {
            return std::nullopt;
        }
        const std::string_view key{reinterpret_cast<const char*>(bytes.data() + offset), keyBytes};
        offset += keyBytes;
        const std::string_view value{reinterpret_cast<const char*>(bytes.data() + offset), valueBytes};
        offset += valueBytes;
        if (current == index)
        {
            return TileMapPropertyView{.key = key, .value = value};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<TileMapPropertyView> findProperty(std::span<const std::byte> bytes, u16 propertyCount,
                                                               std::string_view key) noexcept
{
    for (u16 index = 0; index < propertyCount; ++index)
    {
        const auto property = propertyAt(bytes, propertyCount, index);
        if (!property)
        {
            return std::nullopt;
        }
        if (property->key == key)
        {
            return property;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<TileMapPropertyView> TileMapObjectPayloadView::propertyAt(Core::u16 index) const noexcept
{
    return ::Tina::AssetFormat::propertyAt(propertyBytes, propertyCount, index);
}

std::optional<TileMapPropertyView> TileMapObjectPayloadView::findProperty(std::string_view key) const noexcept
{
    return ::Tina::AssetFormat::findProperty(propertyBytes, propertyCount, key);
}

std::optional<TileMapPropertyView> TileMapLayerPayloadView::propertyAt(Core::u16 index) const noexcept
{
    return ::Tina::AssetFormat::propertyAt(propertyBytes, propertyCount, index);
}

std::optional<TileMapPropertyView> TileMapLayerPayloadView::findProperty(std::string_view key) const noexcept
{
    return ::Tina::AssetFormat::findProperty(propertyBytes, propertyCount, key);
}

std::optional<Core::u16> TileMapLayerPayloadView::tileAt(Core::u32 x, Core::u32 y) const noexcept
{
    if (kind != TileMapLayerKind::Tile || x >= widthCells || y >= heightCells)
    {
        return std::nullopt;
    }
    const usize index = static_cast<usize>(y) * widthCells + x;
    const usize offset = index * sizeof(u16);
    if (!hasBytes(tileBytes, offset, sizeof(u16)))
    {
        return std::nullopt;
    }
    return readU16(tileBytes, offset);
}

std::optional<TileMapObjectPayloadView> TileMapLayerPayloadView::objectAt(Core::u32 index) const noexcept
{
    if (kind != TileMapLayerKind::Object || index >= objectCount)
    {
        return std::nullopt;
    }
    usize offset = 0;
    for (u32 current = 0; current <= index; ++current)
    {
        auto object = parseObjectAt(objectBytes, offset);
        if (!object)
        {
            return std::nullopt;
        }
        if (current == index)
        {
            return object->view;
        }
        offset = object->nextOffset;
    }
    return std::nullopt;
}

std::optional<TileMapObjectPayloadView>
TileMapLayerPayloadView::findObject(TileMapObjectId stableObjectId) const noexcept
{
    if (kind != TileMapLayerKind::Object || stableObjectId == 0)
    {
        return std::nullopt;
    }
    usize offset = 0;
    for (u32 index = 0; index < objectCount; ++index)
    {
        auto object = parseObjectAt(objectBytes, offset);
        if (!object)
        {
            return std::nullopt;
        }
        if (object->view.stableObjectId == stableObjectId)
        {
            return object->view;
        }
        offset = object->nextOffset;
    }
    return std::nullopt;
}

std::optional<TileMapLayerPayloadView> TileMapPayloadView::layerAt(Core::u16 index) const noexcept
{
    if (index >= layerCount)
    {
        return std::nullopt;
    }
    usize offset = 0;
    for (u16 current = 0; current <= index; ++current)
    {
        auto layer = parseLayerAt(layerBytes, offset, widthCells, heightCells);
        if (!layer)
        {
            return std::nullopt;
        }
        if (current == index)
        {
            return layer->view;
        }
        offset = layer->nextOffset;
    }
    return std::nullopt;
}

std::optional<TileMapLayerPayloadView> TileMapPayloadView::findLayer(TileMapLayerId stableLayerId) const noexcept
{
    if (stableLayerId == 0)
    {
        return std::nullopt;
    }
    usize offset = 0;
    for (u16 index = 0; index < layerCount; ++index)
    {
        auto layer = parseLayerAt(layerBytes, offset, widthCells, heightCells);
        if (!layer)
        {
            return std::nullopt;
        }
        if (layer->view.stableLayerId == stableLayerId)
        {
            return layer->view;
        }
        offset = layer->nextOffset;
    }
    return std::nullopt;
}

Core::Result<std::vector<std::byte>> writeTileMapPayloadBytes(const TileMapPayloadDesc& desc)
{
    if (const auto status = validateDesc(desc); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    try
    {
        std::vector<std::byte> bytes;
        appendU16(bytes, TileMapWire::SchemaVersion);
        appendU16(bytes, 0U);
        appendU32(bytes, desc.widthCells);
        appendU32(bytes, desc.heightCells);
        appendF32(bytes, desc.cellSizeMeters);
        appendU16(bytes, static_cast<u16>(desc.layers.size()));
        appendU16(bytes, 0U);
        for (const TileMapLayerDesc& layer : desc.layers)
        {
            appendLayer(bytes, layer);
        }
        return bytes;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "tilemap payload allocation failed");
    }
    catch (const std::length_error&)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded, "tilemap payload exceeds vector size limit");
    }
}

Core::Result<TileMapPayloadView> parseTileMapPayload(std::span<const std::byte> payload)
{
    if (payload.size() < TileMapWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "tilemap payload too small");
    }

    TileMapPayloadView view{
        .schemaVersion = readU16(payload, 0U),
        .flags = readU16(payload, 2U),
        .widthCells = readU32(payload, 4U),
        .heightCells = readU32(payload, 8U),
        .cellSizeMeters = readF32(payload, 12U),
        .layerCount = readU16(payload, 16U),
        .payloadBytes = payload,
    };
    const u16 reserved = readU16(payload, 18U);
    if (view.schemaVersion != TileMapWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema, "unsupported tilemap payload schema");
    }
    if (view.flags != 0U || reserved != 0U)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported tilemap header flags");
    }
    if (view.widthCells == 0 || view.heightCells == 0 || view.widthCells > TileMapWire::MaxDimension ||
        view.heightCells > TileMapWire::MaxDimension || !isFinite(view.cellSizeMeters) || !(view.cellSizeMeters > 0.0f))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap header fields invalid");
    }
    if (view.layerCount == 0 || view.layerCount > TileMapWire::MaxLayers)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLimits, "tilemap layer count out of range");
    }

    view.layerBytes = payload.subspan(TileMapWire::HeaderBytes);
    std::array<TileMapLayerId, TileMapWire::MaxLayers> layerIds{};
    std::array<TileMapObjectId, TileMapWire::MaxObjectsPerMap> objectIds{};
    usize objectIdCount = 0;
    usize offset = 0;
    for (u16 index = 0; index < view.layerCount; ++index)
    {
        auto layer = parseLayerAt(view.layerBytes, offset, view.widthCells, view.heightCells);
        if (!layer)
        {
            return Core::failure(std::move(layer.error()));
        }
        for (u16 prior = 0; prior < index; ++prior)
        {
            if (layerIds[prior] == layer->view.stableLayerId)
            {
                return Core::failure(AssetFormatErrorCode::InvalidIdentity, "duplicate tilemap layer id");
            }
        }
        layerIds[index] = layer->view.stableLayerId;
        if (layer->view.kind == TileMapLayerKind::Object)
        {
            usize objectOffset = 0;
            for (u32 objectIndex = 0; objectIndex < layer->view.objectCount; ++objectIndex)
            {
                auto object = parseObjectAt(layer->view.objectBytes, objectOffset);
                if (!object)
                {
                    return Core::failure(std::move(object.error()));
                }
                if (objectIdCount == objectIds.size())
                {
                    return Core::failure(AssetFormatErrorCode::InvalidLimits, "tilemap object count exceeds limit");
                }
                for (usize prior = 0; prior < objectIdCount; ++prior)
                {
                    if (objectIds[prior] == object->view.stableObjectId)
                    {
                        return Core::failure(AssetFormatErrorCode::InvalidIdentity, "duplicate tilemap object id");
                    }
                }
                objectIds[objectIdCount++] = object->view.stableObjectId;
                objectOffset = object->nextOffset;
            }
        }
        offset = layer->nextOffset;
    }
    if (offset != view.layerBytes.size())
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap payload has trailing bytes");
    }
    return view;
}

Core::Result<std::vector<std::byte>> writeCookedTileMapAsset(Core::AssetId tileMapId, const TileMapPayloadDesc& desc,
                                                             TargetPlatform platform)
{
    if (!tileMapId || !desc.tilesetId)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity, "tilemap requires map id and tileset id");
    }
    auto payload = writeTileMapPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    const std::array deps{CookedAssetWriteDependency{
        .assetId = desc.tilesetId,
        .expectedKind = AssetKind::Tileset,
        .flags = DependencyFlags::Required,
    }};
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::TileMap,
        .assetTypeVersion = TileMapWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = tileMapId,
        .dependencies = deps,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
