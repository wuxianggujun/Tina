#include <gtest/gtest.h>

#include <tina/core/text/JsonDocument.hpp>

#include <cstddef>
#include <limits>
#include <span>
#include <string_view>

namespace Tina::Tests {
namespace {

TEST(JsonDocumentTest, ParsesAndNavigatesNestedValues)
{
    const auto document = Core::JsonDocument::parse(
        R"json({"name":"Tina","enabled":true,"count":42,"items":[null,{"id":7}]})json");
    ASSERT_TRUE(document);

    const auto root = document->root();
    EXPECT_TRUE(root.isObject());
    EXPECT_EQ(root.size(), 4U);
    EXPECT_TRUE(root.contains("items"));

    const auto name = root.member("name");
    ASSERT_TRUE(name);
    ASSERT_TRUE(name->asString());
    EXPECT_EQ(*name->asString(), "Tina");

    const auto enabled = root.member("enabled");
    ASSERT_TRUE(enabled);
    ASSERT_TRUE(enabled->asBoolean());
    EXPECT_TRUE(*enabled->asBoolean());

    const auto count = root.member("count");
    ASSERT_TRUE(count);
    ASSERT_TRUE(count->asSignedInteger());
    EXPECT_EQ(*count->asSignedInteger(), 42);

    const auto items = root.member("items");
    ASSERT_TRUE(items);
    ASSERT_TRUE(items->isArray());
    EXPECT_TRUE(items->element(0)->isNull());
    const auto nested = items->element(1);
    ASSERT_TRUE(nested);
    const auto id = nested->member("id");
    ASSERT_TRUE(id);
    EXPECT_EQ(*id->asSignedInteger(), 7);
}

TEST(JsonDocumentTest, PreservesUnsignedIntegerKind)
{
    const auto document = Core::JsonDocument::parse(R"({"value":18446744073709551615})");
    ASSERT_TRUE(document);

    const auto value = document->root().member("value");
    ASSERT_TRUE(value);
    ASSERT_TRUE(value->numberKind());
    EXPECT_EQ(*value->numberKind(), Core::JsonNumberKind::UnsignedInteger);
    ASSERT_TRUE(value->asUnsignedInteger());
    EXPECT_EQ(*value->asUnsignedInteger(), (std::numeric_limits<Core::u64>::max)());
}

TEST(JsonDocumentTest, ReportsParseAndAccessErrors)
{
    const auto malformed = Core::JsonDocument::parse(R"({"missing":})");
    ASSERT_FALSE(malformed);
    EXPECT_EQ(malformed.error().code, Core::JsonErrorCode::ParseFailed);

    const auto document = Core::JsonDocument::parse(R"({"text":"value","array":[]})");
    ASSERT_TRUE(document);
    const auto root = document->root();
    EXPECT_EQ(root.member("unknown").error().code, Core::JsonErrorCode::MemberNotFound);
    EXPECT_EQ(root.member("text")->asBoolean().error().code, Core::JsonErrorCode::TypeMismatch);
    EXPECT_EQ(root.member("array")->element(0).error().code,
              Core::JsonErrorCode::IndexOutOfRange);
}

TEST(JsonDocumentTest, EnforcesInputDepthAndNodeLimits)
{
    Core::JsonParseOptions inputLimit;
    inputLimit.maxInputBytes = 4U;
    const auto tooLarge = Core::JsonDocument::parse("{}   ", inputLimit);
    ASSERT_FALSE(tooLarge);
    EXPECT_EQ(tooLarge.error().code, Core::JsonErrorCode::LimitExceeded);

    Core::JsonParseOptions depthLimit;
    depthLimit.maxDepth = 1U;
    const auto tooDeep = Core::JsonDocument::parse(R"({"nested":{"value":1}})", depthLimit);
    ASSERT_FALSE(tooDeep);
    EXPECT_EQ(tooDeep.error().code, Core::JsonErrorCode::LimitExceeded);

    Core::JsonParseOptions nodeLimit;
    nodeLimit.maxNodes = 2U;
    const auto tooManyNodes = Core::JsonDocument::parse(R"([1,2])", nodeLimit);
    ASSERT_FALSE(tooManyNodes);
    EXPECT_EQ(tooManyNodes.error().code, Core::JsonErrorCode::LimitExceeded);
}

TEST(JsonDocumentTest, ParsesByteSpan)
{
    constexpr std::string_view text = R"({"ok":true})";
    const auto bytes = std::as_bytes(std::span<const char>{text.data(), text.size()});
    const auto document = Core::JsonDocument::parse(bytes);
    ASSERT_TRUE(document);
    const auto ok = document->root().member("ok");
    ASSERT_TRUE(ok);
    EXPECT_TRUE(*ok->asBoolean());
}

} // namespace
} // namespace Tina::Tests
