#include <tina/serialization/JsonArchive.hpp>
#include <tina/save/SaveStore.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <stdexcept>

namespace Tina::Tests {
struct ArchiveSnapshot final {
    std::string name;
    Core::u64 currency = 0;
    Core::i64 debt = 0;
    std::optional<int> level;
    std::array<float, 3> position{};
    std::map<std::string, std::vector<int>> inventory;
    friend bool operator==(const ArchiveSnapshot&, const ArchiveSnapshot&) = default;
};
}

namespace Tina::Serialization {
template<> struct Codec<Tests::ArchiveSnapshot> {
    static Core::Status encode(Writer& writer, const Tests::ArchiveSnapshot& value) {
        return writer.object([&](ObjectWriter& object) {
            if (auto status = object.field("name", value.name); !status) return status;
            if (auto status = object.field("currency", value.currency); !status) return status;
            if (auto status = object.field("debt", value.debt); !status) return status;
            if (auto status = object.field("level", value.level); !status) return status;
            if (auto status = object.field("position", value.position); !status) return status;
            return object.field("inventory", value.inventory);
        });
    }
    static Core::Result<Tests::ArchiveSnapshot> decode(const Reader& reader) {
        if (auto status = reader.fieldsOnly({"name", "currency", "debt", "level", "position", "inventory"}); !status)
            return Core::failure(status.error());
        auto name = reader.field<std::string>("name");
        if (!name) return Core::failure(name.error());
        auto currency = reader.field<Core::u64>("currency");
        if (!currency) return Core::failure(currency.error());
        auto debt = reader.field<Core::i64>("debt");
        if (!debt) return Core::failure(debt.error());
        auto level = reader.optionalField<int>("level");
        if (!level) return Core::failure(level.error());
        auto position = reader.field<std::array<float, 3>>("position");
        if (!position) return Core::failure(position.error());
        auto inventory = reader.field<std::map<std::string, std::vector<int>>>("inventory");
        if (!inventory) return Core::failure(inventory.error());
        return Tests::ArchiveSnapshot{std::move(*name), *currency, *debt, *level, *position, std::move(*inventory)};
    }
};
}

namespace Tina::Tests {
namespace {
using namespace Serialization;

std::span<const std::byte> bytesOf(std::string_view text)
{
    return std::as_bytes(std::span{text.data(), text.size()});
}

ArchiveSnapshot snapshot()
{
    return {"地下城勇士😀", (std::numeric_limits<Core::u64>::max)(), (std::numeric_limits<Core::i64>::min)(),
            7, {1.25F, -2, 0}, {{"potions", {1, 2, 3}}, {"empty", {}}}};
}

TEST(JsonArchiveTests, RoundTripsStructuredValuesUtf8AndFullWidthIntegers)
{
    const auto original = snapshot();
    const auto bytes = encodeJson(original);
    ASSERT_TRUE(bytes) << bytes.error().message;
    const auto restored = decodeJson<ArchiveSnapshot>(*bytes);
    ASSERT_TRUE(restored) << restored.error().message;
    EXPECT_EQ(*restored, original);
    auto null = encodeJson(std::optional<int>{});
    ASSERT_TRUE(null);
    auto empty = decodeJson<std::optional<int>>(*null);
    ASSERT_TRUE(empty);
    EXPECT_FALSE(empty->has_value());
    auto booleans = encodeJson(std::vector<bool>{true, false});
    ASSERT_TRUE(booleans);
    EXPECT_EQ(decodeJson<std::vector<bool>>(*booleans).value(), (std::vector<bool>{true, false}));
}

TEST(JsonArchiveTests, ReaderKeepsItsDocumentAliveAndReportsFieldPaths)
{
    auto reader = decodeJson(bytesOf(R"({"schema":1,"data":{"items":[1,"bad"]}})"));
    ASSERT_TRUE(reader);
    const auto failed = reader->field<std::vector<int>>("items");
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, ErrorCode::InvalidData);
    ASSERT_FALSE(failed.error().context.empty());
    EXPECT_EQ(failed.error().context.back().detail, "$.data.items[1]");
    auto absent = reader->optionalField<int>("absent");
    ASSERT_TRUE(absent);
    EXPECT_FALSE(absent->has_value());
    EXPECT_FALSE(reader->field<int>("absent"));
    EXPECT_FALSE(reader->fieldsOnly({"allowed"}));
}

TEST(JsonArchiveTests, RejectsOldEnvelopesDuplicateFieldsAndWrongScalarKinds)
{
    for (const auto text : {R"({"schema":0,"data":1})", R"({"schema":2,"data":1})",
                           R"({"schema":1})", R"({"schema":1,"data":1,"extra":2})",
                           R"({"schema":1,"data":{"x":1,"\u0078":2}})", R"({"data":1})"}) {
        SCOPED_TRACE(text);
        EXPECT_FALSE(decodeJson(bytesOf(text)));
    }
    EXPECT_FALSE(decodeJson<Core::u8>(bytesOf(R"({"schema":1,"data":256})")));
    EXPECT_FALSE(decodeJson<Core::u64>(bytesOf(R"({"schema":1,"data":-1})")));
    EXPECT_FALSE(decodeJson<int>(bytesOf(R"({"schema":1,"data":1.5})")));
    EXPECT_FALSE(decodeJson<bool>(bytesOf(R"({"schema":1,"data":1})")));
    EXPECT_FALSE((decodeJson<std::array<int, 2>>(bytesOf(R"({"schema":1,"data":[1]})"))));
    EXPECT_FALSE(encodeJson(std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(encodeJson(std::string{"\xC0\xAF"}));
    const auto largestLongDouble = encodeJson((std::numeric_limits<long double>::max)());
    if constexpr ((std::numeric_limits<long double>::max)() > (std::numeric_limits<double>::max)()) {
        EXPECT_FALSE(largestLongDouble);
        EXPECT_FALSE(encodeJson(-(std::numeric_limits<long double>::max)()));
    } else {
        EXPECT_TRUE(largestLongDouble);
        EXPECT_TRUE(encodeJson(-(std::numeric_limits<long double>::max)()));
    }
}

TEST(JsonArchiveTests, ObjectMapEnumerationPreservesChildPathsAndLargeMaps)
{
    std::map<std::string, int> original;
    for (int index = 0; index < 4096; ++index) original.emplace("item_" + std::to_string(index), index);
    auto bytes = encodeJson(original);
    ASSERT_TRUE(bytes);
    EXPECT_EQ((decodeJson<std::map<std::string, int>>(*bytes).value()), original);
    auto reader = decodeJson(bytesOf(R"({"schema":1,"data":{"quantity":"bad"}})"));
    ASSERT_TRUE(reader);
    auto failed = reader->as<std::map<std::string, int>>();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().context.back().detail, "$.data.quantity");
}

TEST(JsonArchiveTests, WriterErrorsRemainStickyEvenWhenCodecsIgnoreThem)
{
    auto duplicate = encodeJsonWith([](Writer& writer) { return writer.object([](ObjectWriter& object) {
        if (auto status = object.field("hp", 1); !status) return status;
        (void)object.field("hp", 2);
        return Core::success();
    }); });
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, ErrorCode::DuplicateIdentity);
    auto empty = encodeJsonWith([](Writer&) { return Core::success(); });
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error().code, ErrorCode::CodecFailed);
    auto multiple = encodeJsonWith([](Writer& writer) {
        if (auto status = writer.number(1); !status) return status;
        (void)writer.number(2);
        return Core::success();
    });
    ASSERT_FALSE(multiple);
    EXPECT_EQ(multiple.error().code, ErrorCode::CodecFailed);
    auto thrown = encodeJsonWith([](Writer&) -> Core::Status { throw std::runtime_error("test codec failure"); });
    ASSERT_FALSE(thrown);
    EXPECT_EQ(thrown.error().code, ErrorCode::CodecFailed);
    EXPECT_EQ(thrown.error().message, "test codec failure");
}

TEST(JsonArchiveTests, EncodeAndDecodeEnforceTheSameDepthAndNodeBudgets)
{
    const std::vector<std::vector<int>> nested{{1, 2}};
    const auto unrestricted = encodeJson(nested);
    ASSERT_TRUE(unrestricted);
    for (Core::usize depth = 1; depth <= 4; ++depth) {
        const ArchiveLimits limits{.maxDepth = depth};
        EXPECT_EQ(encodeJson(nested, limits).has_value(), depth >= 3);
        EXPECT_EQ(decodeJson<std::vector<std::vector<int>>>(*unrestricted, limits).has_value(), depth >= 3);
    }
    auto scalar = encodeJson(42, {.maxDepth = 1, .maxNodes = 3});
    ASSERT_TRUE(scalar);
    EXPECT_EQ(decodeJson<int>(*scalar, {.maxDepth = 1, .maxNodes = 3}).value(), 42);
    EXPECT_FALSE(encodeJson(std::vector<int>{1, 2}, {.maxNodes = 4}));
    EXPECT_FALSE(decodeJson(*unrestricted, {.maxNodes = 4}));
    EXPECT_FALSE(encodeJson(std::string(100, 'x'), {.maxBytes = 20}));
    EXPECT_FALSE(decodeJson(*unrestricted, {.maxBytes = 5}));
}

class SerializedSaveTest : public testing::Test {
  protected:
    void SetUp() override {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() / ("tina_structured_save_" + std::to_string(unique));
        const auto utf8 = root_.generic_u8string();
        auto store = Save::SaveStore::Create({.rootDirectoryUtf8 = {utf8.begin(), utf8.end()}, .gameId = "tina.serialization"});
        ASSERT_TRUE(store);
        store_ = std::move(*store);
    }
    void TearDown() override {
        store_.reset();
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        EXPECT_FALSE(error) << error.message();
    }
    std::filesystem::path root_;
    std::unique_ptr<Save::SaveStore> store_;
};

TEST_F(SerializedSaveTest, SaveStorePersistsOnlyTheArchiveBytesAndRestoresDetachedState)
{
    const auto state = snapshot();
    auto encoded = encodeJson(state);
    ASSERT_TRUE(encoded);
    const auto saved = store_->saveSlot({.slot = {7}, .dataVersion = 1, .displayName = "冒险存档", .payload = *encoded});
    ASSERT_TRUE(saved) << saved.error().message;
    const auto loaded = store_->loadSlot({7});
    ASSERT_TRUE(loaded) << loaded.error().message;
    EXPECT_EQ(loaded->payload, *encoded);
    EXPECT_EQ(decodeJson<ArchiveSnapshot>(loaded->payload).value(), state);
}

} // namespace
} // namespace Tina::Tests
