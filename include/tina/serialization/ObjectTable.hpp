#pragma once

#include <tina/serialization/TypeRegistry.hpp>

namespace Tina::Serialization {

// Persistent identity, not an Entity, pointer, or generation handle. Zero is a
// nullable reference, never a stored object's identity.
struct ObjectId final {
    Core::u64 value = 0;
    friend auto operator<=>(const ObjectId&, const ObjectId&) = default;
};

template<> struct Codec<ObjectId> {
    static Core::Status encode(Writer& writer, ObjectId id) { return writer.number(id.value); }
    static Core::Result<ObjectId> decode(const Reader& reader) {
        auto id = reader.as<Core::u64>();
        if (!id) return Core::failure(id.error());
        return ObjectId{*id};
    }
};

// Decode into a detached candidate table, resolve/validate all game references,
// then move the complete table into live state. Failure destroys the candidate;
// no partially restored objects are published. Borrowed pointers remain valid
// until their object/table is destroyed or replaced (moving preserves pointees).
template<class Base> class ObjectTable final {
  public:
    ObjectTable() = default;
    ObjectTable(const ObjectTable&) = delete;
    ObjectTable& operator=(const ObjectTable&) = delete;
    ObjectTable(ObjectTable&&) noexcept = default;
    ObjectTable& operator=(ObjectTable&&) noexcept = default;

    [[nodiscard]] Core::Status insert(ObjectId id, std::unique_ptr<Base> object)
    try {
        if (!id.value || !object) return Core::failure(ErrorCode::InvalidData, "Object requires a nonzero ID and owner");
        if (objects_.contains(id)) return Core::failure(ErrorCode::DuplicateIdentity, "Duplicate persistent object ID");
        objects_.emplace(id, std::move(object));
        return Core::success();
    } catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Object table allocation failed"); }

    [[nodiscard]] Core::Result<Base*> resolve(ObjectId id) const {
        if (!id.value) return static_cast<Base*>(nullptr);
        auto found = objects_.find(id);
        if (found == objects_.end()) return Core::failure(ErrorCode::UnresolvedReference, "Persistent object reference was not found");
        return found->second.get();
    }
    [[nodiscard]] const auto& objects() const noexcept { return objects_; }

    [[nodiscard]] Core::Status encode(Writer& writer, const TypeRegistry<Base>& registry) const {
        return writer.array([&](ArrayWriter& array) {
            for (const auto& [id, object] : objects_) {
                auto status = array.elementWith([&](Writer& item) { return item.object([&](ObjectWriter& fields) {
                    if (auto result = fields.field("id", id); !result) return result;
                    return fields.fieldWith("object", [&](Writer& value) { return registry.encode(value, object.get()); });
                }); });
                if (!status) return status;
            }
            return Core::success();
        });
    }

    [[nodiscard]] static Core::Result<ObjectTable> decode(const Reader& reader, const TypeRegistry<Base>& registry)
    try {
        if (!reader.value().isArray()) return Core::failure(reader.error(ErrorCode::InvalidData, "Expected object table array"));
        ObjectTable candidate;
        for (Core::usize index = 0; index < reader.size(); ++index) {
            auto item = reader.element(index);
            if (!item) return Core::failure(item.error());
            if (auto status = item->fieldsOnly({"id", "object"}); !status) return Core::failure(status.error());
            auto id = item->template field<ObjectId>("id");
            if (!id) return Core::failure(id.error());
            if (!id->value) return Core::failure(item->error(ErrorCode::InvalidData, "Object identity must be nonzero"));
            if (candidate.objects_.contains(*id))
                return Core::failure(item->error(ErrorCode::DuplicateIdentity, "Duplicate persistent object ID"));
            auto data = item->member("object");
            if (!data) return Core::failure(data.error());
            auto object = registry.decode(*data);
            if (!object) return Core::failure(object.error());
            if (auto status = candidate.insert(*id, std::move(*object)); !status)
                return Core::failure(item->error(status.error().code, "Invalid or duplicate object identity"));
        }
        return candidate;
    } catch (const std::bad_alloc&) { return Core::failure(reader.error(Core::CoreErrorCode::OutOfMemory, "Object table allocation failed")); }

  private:
    std::map<ObjectId, std::unique_ptr<Base>> objects_;
};

} // namespace Tina::Serialization
