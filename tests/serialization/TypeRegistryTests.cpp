#include <tina/serialization/ObjectTable.hpp>
#include <gtest/gtest.h>

#include <stdexcept>

namespace Tina::Tests {
struct SavedObject { virtual ~SavedObject() = default; };
struct SavedEnemy : virtual SavedObject {
    int health = 100;
    Serialization::ObjectId target{};
};
struct SavedEliteEnemy : SavedEnemy {};
struct SavedChest : SavedObject { int coins = 2; };
}

namespace Tina::Serialization {
template<> struct Codec<Tests::SavedEnemy> {
    static Core::Status encode(Writer& writer, const Tests::SavedEnemy& value) {
        return writer.object([&](ObjectWriter& object) {
            if (auto status = object.field("health", value.health); !status) return status;
            return object.field("target", value.target);
        });
    }
    static Core::Result<Tests::SavedEnemy> decode(const Reader& reader) {
        if (auto status = reader.fieldsOnly({"health", "target"}); !status) return Core::failure(status.error());
        auto health = reader.field<int>("health");
        if (!health) return Core::failure(health.error());
        auto target = reader.field<ObjectId>("target");
        if (!target) return Core::failure(target.error());
        Tests::SavedEnemy result;
        result.health = *health; result.target = *target;
        return result;
    }
};
template<> struct Codec<Tests::SavedChest> {
    static Core::Status encode(Writer& writer, const Tests::SavedChest& value) { return writer.number(value.coins); }
    static Core::Result<Tests::SavedChest> decode(const Reader& reader) {
        auto coins = reader.as<int>();
        if (!coins) return Core::failure(coins.error());
        Tests::SavedChest result; result.coins = *coins; return result;
    }
};
}

namespace Tina::Tests {
namespace {
using namespace Serialization;

Core::Result<Reader> archiveReader(std::string_view json)
{
    return decodeJson(std::as_bytes(std::span{json.data(), json.size()}));
}

TEST(TypeRegistryTests, StableIdsRoundTripAndRegistryInstancesAreIndependent)
{
    TypeRegistry<SavedObject> registry;
    ASSERT_TRUE(registry.registerType<SavedEnemy>("game.enemy"));
    SavedEnemy enemy; enemy.health = 23; enemy.target = {42};
    auto encoded = encodeJsonWith([&](Writer& writer) { return registry.encode(writer, &enemy); });
    ASSERT_TRUE(encoded);
    auto reader = decodeJson(*encoded);
    ASSERT_TRUE(reader);
    EXPECT_EQ(reader->field<std::string>("type").value(), "game.enemy");
    auto restored = registry.decode(*reader);
    ASSERT_TRUE(restored);
    const auto* typed = dynamic_cast<SavedEnemy*>(restored->get());
    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->health, 23);
    EXPECT_EQ(typed->target.value, 42U);
    TypeRegistry<SavedObject> emptyRegistry;
    auto unknown = emptyRegistry.decode(*reader);
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code, ErrorCode::UnknownType);
    auto nullable = encodeJsonWith([&](Writer& writer) { return registry.encode(writer, nullptr); });
    ASSERT_TRUE(nullable);
    auto nullObject = registry.decode(decodeJson(*nullable).value());
    ASSERT_TRUE(nullObject);
    EXPECT_EQ(nullObject->get(), nullptr);
}

TEST(TypeRegistryTests, RejectsDuplicateIdsNativeTypesInvalidIdsAndCapacityOverflow)
{
    TypeRegistry<SavedObject> registry{1};
    ASSERT_TRUE(registry.registerType<SavedEnemy>("game.enemy"));
    EXPECT_EQ(registry.registerType<SavedChest>("game.enemy").error().code, ErrorCode::DuplicateIdentity);
    EXPECT_EQ(registry.registerType<SavedEnemy>("renamed.enemy").error().code, ErrorCode::DuplicateIdentity);
    EXPECT_EQ(registry.registerType<SavedChest>("game.chest").error().code, ErrorCode::LimitExceeded);
    TypeRegistry<SavedObject> other;
    EXPECT_FALSE(other.registerType<SavedEnemy>(""));
    EXPECT_FALSE(other.registerType<SavedEnemy>("not a type"));
    EXPECT_FALSE(other.registerType<SavedEnemy>("中文"));
    SavedChest unregistered;
    EXPECT_FALSE(encodeJsonWith([&](Writer& writer) { return registry.encode(writer, &unregistered); }));
}

TEST(TypeRegistryTests, RejectsNullWrongDynamicTypeAndThrowingFactories)
{
    auto reader = archiveReader(R"({"schema":1,"data":{"type":"enemy","data":{"health":1,"target":0}}})");
    ASSERT_TRUE(reader);
    TypeRegistry<SavedObject> nullRegistry;
    ASSERT_TRUE(nullRegistry.registerType<SavedEnemy>("enemy", [](const Reader&) -> Core::Result<std::unique_ptr<SavedEnemy>> {
        return std::unique_ptr<SavedEnemy>{};
    }));
    EXPECT_EQ(nullRegistry.decode(*reader).error().code, ErrorCode::CodecFailed);
    TypeRegistry<SavedObject> wrongRegistry;
    ASSERT_TRUE(wrongRegistry.registerType<SavedEnemy>("enemy", [](const Reader&) -> Core::Result<std::unique_ptr<SavedEnemy>> {
        return std::unique_ptr<SavedEnemy>{std::make_unique<SavedEliteEnemy>()};
    }));
    EXPECT_EQ(wrongRegistry.decode(*reader).error().code, ErrorCode::CodecFailed);
    TypeRegistry<SavedObject> throwingRegistry;
    ASSERT_TRUE(throwingRegistry.registerType<SavedEnemy>("enemy", [](const Reader&) -> Core::Result<std::unique_ptr<SavedEnemy>> {
        throw std::runtime_error("factory failed");
    }));
    EXPECT_EQ(throwingRegistry.decode(*reader).error().code, ErrorCode::CodecFailed);
}

TEST(ObjectTableTests, DetachedRestoreResolvesCyclesBeforeTheGamePublishesIt)
{
    TypeRegistry<SavedObject> registry;
    ASSERT_TRUE(registry.registerType<SavedEnemy>("enemy"));
    ObjectTable<SavedObject> original;
    auto first = std::make_unique<SavedEnemy>(); first->target = {2};
    auto second = std::make_unique<SavedEnemy>(); second->target = {1};
    ASSERT_TRUE(original.insert({1}, std::move(first)));
    ASSERT_TRUE(original.insert({2}, std::move(second)));
    const auto encoded = encodeJsonWith([&](Writer& writer) { return original.encode(writer, registry); });
    ASSERT_TRUE(encoded);
    auto candidate = ObjectTable<SavedObject>::decode(decodeJson(*encoded).value(), registry);
    ASSERT_TRUE(candidate);
    for (const auto& [id, object] : candidate->objects()) {
        (void)id;
        const auto* enemy = dynamic_cast<const SavedEnemy*>(object.get());
        ASSERT_NE(enemy, nullptr);
        EXPECT_TRUE(candidate->resolve(enemy->target));
    }
    auto* borrowed = candidate->resolve({1}).value();
    ObjectTable<SavedObject> live = std::move(*candidate);
    EXPECT_EQ(live.resolve({1}).value(), borrowed);
    EXPECT_EQ(live.resolve({0}).value(), nullptr);
    EXPECT_EQ(live.resolve({99}).error().code, ErrorCode::UnresolvedReference);
}

TEST(ObjectTableTests, InvalidCandidateCannotReplaceLiveStateOrInvokeDuplicateFactories)
{
    int factories = 0;
    TypeRegistry<SavedObject> registry;
    ASSERT_TRUE(registry.registerType<SavedEnemy>("enemy", [&](const Reader& reader) -> Core::Result<std::unique_ptr<SavedEnemy>> {
        ++factories;
        auto value = reader.as<SavedEnemy>();
        if (!value) return Core::failure(value.error());
        return std::make_unique<SavedEnemy>(std::move(*value));
    }));
    ObjectTable<SavedObject> live;
    ASSERT_TRUE(live.insert({7}, std::make_unique<SavedEnemy>()));
    auto* old = live.resolve({7}).value();
    const auto duplicate = archiveReader(R"({"schema":1,"data":[
        {"id":1,"object":{"type":"enemy","data":{"health":3,"target":0}}},
        {"id":1,"object":{"type":"enemy","data":{"health":4,"target":0}}}]})");
    ASSERT_TRUE(duplicate);
    const auto candidate = ObjectTable<SavedObject>::decode(*duplicate, registry);
    ASSERT_FALSE(candidate);
    EXPECT_EQ(candidate.error().code, ErrorCode::DuplicateIdentity);
    EXPECT_EQ(factories, 1);
    EXPECT_EQ(live.resolve({7}).value(), old);
    const auto zero = archiveReader(R"({"schema":1,"data":[{"id":0,"object":null}]})");
    ASSERT_TRUE(zero);
    EXPECT_FALSE(ObjectTable<SavedObject>::decode(*zero, registry));
    EXPECT_EQ(factories, 1);
}

} // namespace
} // namespace Tina::Tests
