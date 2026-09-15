#pragma once

#include <tina/serialization/JsonArchive.hpp>

#include <concepts>
#include <memory>
#include <typeindex>

namespace Tina::Serialization {

// Owned by the game composition root. Register during startup; share only const
// access while decoding. C++ RTTI is used in memory, never written to the archive.
template<class Base> class TypeRegistry final {
    static_assert(std::has_virtual_destructor_v<Base>, "Polymorphic ownership requires a virtual destructor");
  public:
    explicit TypeRegistry(Core::usize maxTypes = 256) : maxTypes_(maxTypes) {}

    template<std::derived_from<Base> Derived>
    [[nodiscard]] Core::Status registerType(std::string typeId)
    {
        return registerType<Derived>(std::move(typeId), [](const Reader& reader) -> Core::Result<std::unique_ptr<Derived>> {
            auto decoded = reader.template as<Derived>();
            if (!decoded) return Core::failure(decoded.error());
            return std::make_unique<Derived>(std::move(*decoded));
        });
    }

    template<std::derived_from<Base> Derived>
    [[nodiscard]] Core::Status registerType(std::string typeId,
        std::function<Core::Result<std::unique_ptr<Derived>>(const Reader&)> factory)
    try {
        if (typeId.empty() || typeId.size() > 128 || !factory)
            return Core::failure(ErrorCode::InvalidData, "Invalid type registration");
        for (const unsigned char character : typeId) {
            if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                  (character >= '0' && character <= '9') || character == '.' || character == '_' ||
                  character == '-' || character == ':' || character == '/'))
                return Core::failure(ErrorCode::InvalidData, "Type ID must be a stable ASCII identifier");
        }
        for (const auto& entry : entries_) {
            if (entry.id == typeId || entry.nativeType == std::type_index(typeid(Derived)))
                return Core::failure(ErrorCode::DuplicateIdentity, "Duplicate type ID or C++ type");
        }
        if (entries_.size() >= maxTypes_)
            return Core::failure(ErrorCode::LimitExceeded, "Type registry capacity exceeded");
        Entry entry{std::move(typeId), std::type_index(typeid(Derived)),
            [](Writer& writer, const Base& value) {
                const auto* derived = dynamic_cast<const Derived*>(&value);
                if (!derived) return writer.fail("Registered object has the wrong base type", ErrorCode::CodecFailed);
                return Codec<Derived>::encode(writer, *derived);
            },
            [factory = std::move(factory)](const Reader& reader) -> Core::Result<std::unique_ptr<Base>> {
                auto object = factory(reader);
                if (!object) return Core::failure(object.error());
                if (!*object) return Core::failure(reader.error(ErrorCode::CodecFailed, "Factory returned null"));
                return std::unique_ptr<Base>{std::move(*object)};
            }};
        entries_.push_back(std::move(entry));
        return Core::success();
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "Type registry allocation failed");
    } catch (const std::exception& exception) { return Core::failure(ErrorCode::CodecFailed, exception.what()); }
      catch (...) { return Core::failure(ErrorCode::CodecFailed, "Type registration threw"); }

    // Nullable owning pointer envelope: null, or {"type":"game.enemy", "data":{...}}.
    [[nodiscard]] Core::Status encode(Writer& writer, const Base* object) const
    {
        if (!object) return writer.null();
        for (const auto& entry : entries_) {
            if (entry.nativeType != std::type_index(typeid(*object))) continue;
            return writer.object([&](ObjectWriter& output) {
                if (auto status = output.field("type", entry.id); !status) return status;
                return output.fieldWith("data", [&](Writer& child) { return entry.encode(child, *object); });
            });
        }
        return writer.fail("Unregistered dynamic type", ErrorCode::UnknownType);
    }

    [[nodiscard]] Core::Result<std::unique_ptr<Base>> decode(const Reader& reader) const
    try {
        if (reader.value().isNull()) return std::unique_ptr<Base>{};
        if (auto status = reader.fieldsOnly({"type", "data"}); !status) return Core::failure(status.error());
        auto typeId = reader.template field<std::string>("type");
        if (!typeId) return Core::failure(typeId.error());
        auto data = reader.member("data");
        if (!data) return Core::failure(data.error());
        for (const auto& entry : entries_) {
            if (entry.id == *typeId) {
                auto object = entry.decode(*data);
                if (!object) return Core::failure(object.error());
                if (!*object || std::type_index(typeid(**object)) != entry.nativeType)
                    return Core::failure(reader.error(ErrorCode::CodecFailed, "Factory returned the wrong dynamic type"));
                return object;
            }
        }
        return Core::failure(reader.error(ErrorCode::UnknownType, "Type ID is not registered"));
    } catch (const std::bad_alloc&) {
        return Core::failure(reader.error(Core::CoreErrorCode::OutOfMemory, "Factory allocation failed"));
    } catch (const std::exception& exception) {
        return Core::failure(reader.error(ErrorCode::CodecFailed, exception.what()));
    } catch (...) { return Core::failure(reader.error(ErrorCode::CodecFailed, "Factory threw")); }

  private:
    struct Entry final {
        std::string id;
        std::type_index nativeType;
        std::function<Core::Status(Writer&, const Base&)> encode;
        std::function<Core::Result<std::unique_ptr<Base>>(const Reader&)> decode;
    };
    Core::usize maxTypes_;
    std::vector<Entry> entries_;
};

} // namespace Tina::Serialization
