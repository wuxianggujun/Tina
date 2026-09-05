#include <tina/core/text/JsonWriter.hpp>

#include <gtest/gtest.h>

#include <sstream>
#include <string>

namespace Tina::Tests {
namespace {

[[nodiscard]] std::string writeToString(auto&& body)
{
    std::ostringstream output;
    Core::JsonWriter writer(output);
    body(writer);
    EXPECT_TRUE(writer.balanced());
    EXPECT_FALSE(writer.failed());
    return output.str();
}

// The gates select their line with an anchored regex that requires "status" first and "sample"
// second, adjacent -- RunProduct2dGate.ps1:325 and five siblings. Insertion order is therefore a
// contract, not a convenience, and a writer that sorted keys would break them at runtime.
TEST(JsonWriterTest, KeysComeOutInInsertionOrderNotSorted)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.member("status", "ok");
        writer.member("sample", "tina_sample_2d");
        writer.member("frames", 300U);
        writer.endObject();
    });

    EXPECT_EQ(json, R"({"status":"ok","sample":"tina_sample_2d","frames":300})");
}

// Around 130 evidence patterns in RunProduct2dGate.ps1:148-280 are spelled 'evidenceSchema\":29',
// so a space after ':' or ',' would stop them matching.
TEST(JsonWriterTest, OutputIsCompactWithNoSpaceAfterColonOrComma)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.member("evidenceSchema", 29);
        writer.member("renderExtractions", 12);
        writer.endObject();
    });

    EXPECT_EQ(json, R"({"evidenceSchema":29,"renderExtractions":12})");
    EXPECT_EQ(json.find(": "), std::string::npos);
    EXPECT_EQ(json.find(", "), std::string::npos);
}

// The separator is the reason this type exists: no call site spells a comma, so no call site can
// omit one or leave a trailing one.
TEST(JsonWriterTest, EmitsSeparatorsBetweenValuesButNeverLeadingOrTrailing)
{
    EXPECT_EQ(writeToString([](Core::JsonWriter& writer) {
                  writer.beginObject();
                  writer.endObject();
              }),
              "{}");

    EXPECT_EQ(writeToString([](Core::JsonWriter& writer) {
                  writer.beginArray();
                  writer.endArray();
              }),
              "[]");

    EXPECT_EQ(writeToString([](Core::JsonWriter& writer) {
                  writer.beginObject();
                  writer.member("only", 1);
                  writer.endObject();
              }),
              R"({"only":1})");

    EXPECT_EQ(writeToString([](Core::JsonWriter& writer) {
                  writer.beginArray();
                  writer.element(1);
                  writer.element(2);
                  writer.element(3);
                  writer.endArray();
              }),
              "[1,2,3]");
}

// Regression: pointer-to-bool is a standard conversion and string_view's is user-defined, so a
// bare `const char*` overload set writes a string literal as `true`. Every spelling of a string a
// call site can hand over must come out quoted.
TEST(JsonWriterTest, StringLiteralsAndStdStringAreQuotedNotConvertedToBool)
{
    const std::string owned = "owned";
    const char* const pointer = "pointer";
    constexpr std::string_view view = "view";

    const std::string json = writeToString([&](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.member("literal", "literal");
        writer.member("owned", owned);
        writer.member("pointer", pointer);
        writer.member("view", view);
        writer.endObject();
    });

    EXPECT_EQ(json,
              R"({"literal":"literal","owned":"owned","pointer":"pointer","view":"view"})");
}

TEST(JsonWriterTest, StringElementsAreQuotedNotConvertedToBool)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginArray();
        writer.element("literal");
        writer.element(std::string("owned"));
        writer.endArray();
    });

    EXPECT_EQ(json, R"(["literal","owned"])");
}

TEST(JsonWriterTest, BooleansUseJsonLiteralsNotIntegers)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.member("autoDemo", true);
        writer.member("dialogOpen", false);
        writer.endObject();
    });

    EXPECT_EQ(json, R"({"autoDemo":true,"dialogOpen":false})");
}

// nlohmann/json owns string escaping, including all JSON control-byte spellings.
TEST(JsonWriterTest, EscapesTheSameCharactersTheHandCopiedHelpersDid)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.member("quote", "a\"b");
        writer.member("backslash", "a\\b");
        writer.member("backspace", "a\bb");
        writer.member("formfeed", "a\fb");
        writer.member("newline", "a\nb");
        writer.member("carriage", "a\rb");
        writer.member("tab", "a\tb");
        writer.endObject();
    });

    EXPECT_EQ(json,
              R"({"quote":"a\"b","backslash":"a\\b","backspace":"a\bb","formfeed":"a\fb")"
              R"(,"newline":"a\nb","carriage":"a\rb","tab":"a\tb"})");
}

// Control bytes below 0x20 with no short escape become \u00xx in lowercase hex, exactly as the old
// helpers spelled them. A NUL is escaped rather than terminating the string, which is why the input
// carries an explicit length. The expected text is assembled from escape sequences so that no real
// control byte ends up in this source file.
TEST(JsonWriterTest, EscapesOtherControlBytesAsLowercaseFourDigitHex)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.member("nul", std::string_view("a\0b", 3U));
        writer.member("unit", std::string_view("a\x1F" "b", 3U));
        writer.member("escape", std::string_view("a\x1B" "b", 3U));
        writer.endObject();
    });

    const std::string escapePrefix = std::string(1U, '\\') + "u00";
    const std::string expected = std::string(R"({"nul":"a)") + escapePrefix + "00" +
                                 R"(b","unit":"a)" + escapePrefix + "1f" +
                                 R"(b","escape":"a)" + escapePrefix + "1b" + R"(b"})";
    EXPECT_EQ(json, expected);
}

// Invalid UTF-8 is replaced during serialization so diagnostic output remains valid JSON.
TEST(JsonWriterTest, ReplacesMalformedUtf8DuringSerialization)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.member("valid", "\xE4\xB8\xAD");
        writer.member("truncated", "\xE4\xB8");
        writer.endObject();
    });

    const std::string replacement = "\xEF\xBF\xBD";
    EXPECT_EQ(json, std::string("{\"valid\":\"\xE4\xB8\xAD\",\"truncated\":\"") +
                       replacement + "\"}");
}

TEST(JsonWriterTest, KeysAreEscapedLikeValues)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.member("a\"b", 1);
        writer.endObject();
    });

    EXPECT_EQ(json, R"({"a\"b":1})");
}

TEST(JsonWriterTest, NestsObjectsAndArraysAsMembers)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.member("status", "ok");
        writer.beginObjectMember("counters");
        writer.member("frames", 300U);
        writer.endObject();
        writer.beginArrayMember("stages");
        writer.element("load");
        writer.element("render");
        writer.endArray();
        writer.endObject();
    });

    EXPECT_EQ(json,
              R"({"status":"ok","counters":{"frames":300},"stages":["load","render"]})");
}

TEST(JsonWriterTest, NestsObjectsInsideArrays)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginArray();
        writer.beginObjectElement();
        writer.member("index", 0);
        writer.endObject();
        writer.beginObjectElement();
        writer.member("index", 1);
        writer.endObject();
        writer.endArray();
    });

    EXPECT_EQ(json, R"([{"index":0},{"index":1}])");
}

TEST(JsonWriterTest, NestsArraysInsideArrays)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginArray();
        writer.beginArrayElement();
        writer.element(1);
        writer.endArray();
        writer.beginArrayElement();
        writer.element(2);
        writer.endArray();
        writer.endArray();
    });

    EXPECT_EQ(json, "[[1],[2]]");
}

// Numbers are formatted by nlohmann's deterministic serializer.
TEST(JsonWriterTest, UsesNlohmannNumberFormatting)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.member("u32", static_cast<Core::u32>(4294967295U));
        writer.member("i32", static_cast<Core::i32>(-2147483647));
        writer.member("usize", static_cast<Core::usize>(0U));
        writer.member("scale", 1.5F);
        writer.member("whole", 2.0F);
        writer.member("double", 0.25);
        writer.endObject();
    });

    EXPECT_EQ(json,
              R"({"u32":4294967295,"i32":-2147483647,"usize":0,"scale":1.5,"whole":2.0,"double":0.25})");
}

// A u16 domain value must print as a number, not as a character.
TEST(JsonWriterTest, WritesSixteenBitValuesAsNumbers)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.member("domain", static_cast<Core::u16>(15U));
        writer.endObject();
    });

    EXPECT_EQ(json, R"({"domain":15})");
}

TEST(JsonWriterTest, RawMemberIsCanonicalizedBySerialization)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.rawMember("milliseconds", "1.250");
        writer.member("frames", 2);
        writer.endObject();
    });

    EXPECT_EQ(json, R"({"milliseconds":1.25,"frames":2})");
}

TEST(JsonWriterTest, RawMemberIsParsedByNlohmannBeforeInsertion)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.rawMember("nested", R"({"count":2,"enabled":true})");
        writer.endObject();
    });

    EXPECT_EQ(json, R"({"nested":{"count":2,"enabled":true}})");
}

TEST(JsonWriterTest, RejectsMalformedRawMember)
{
    std::ostringstream output;
    Core::JsonWriter writer(output);
    writer.beginObject();
    writer.rawMember("malformed", "{");

    EXPECT_TRUE(writer.failed());
    EXPECT_FALSE(writer.balanced());
    EXPECT_TRUE(output.str().empty());
}

TEST(JsonWriterTest, ReportsDepthAndBalance)
{
    std::ostringstream output;
    Core::JsonWriter writer(output);

    EXPECT_TRUE(writer.balanced());
    EXPECT_EQ(writer.depth(), 0U);

    writer.beginObject();
    EXPECT_FALSE(writer.balanced());
    EXPECT_EQ(writer.depth(), 1U);

    writer.beginObjectMember("nested");
    EXPECT_EQ(writer.depth(), 2U);

    writer.endObject();
    writer.endObject();
    EXPECT_TRUE(writer.balanced());
    EXPECT_EQ(writer.depth(), 0U);
}

TEST(JsonWriterTest, WriteStringQuotesAValueOutsideAnyScope)
{
    std::ostringstream output;
    Core::JsonWriter writer(output);
    writer.writeString("a\"b");

    EXPECT_EQ(output.str(), R"("a\"b")");
    EXPECT_TRUE(writer.balanced());
}

// The shape the reports actually emit: one line, one object, gate-anchored prefix.
TEST(JsonWriterTest, ReproducesTheReportShapeTheGatesSelect)
{
    std::ostringstream output;
    {
        Core::JsonWriter writer(output);
        writer.beginObject();
        writer.member("status", "ok");
        writer.member("sample", "tina_sample_2d");
        writer.member("frames", 300U);
        writer.member("renderExtractions", 300U);
        writer.member("autoDemo", false);
        writer.endObject();
    }
    output << '\n';

    const std::string line = output.str();
    EXPECT_TRUE(line.starts_with(R"({"status":"ok","sample":"tina_sample_2d")"));
    EXPECT_TRUE(line.ends_with("}\n"));
    EXPECT_EQ(line.find('\n'), line.size() - 1U);
}

TEST(JsonWriterTest, ErrorReportShapeMatchesTheOldWriteErrorHelpers)
{
    const std::string json = writeToString([](Core::JsonWriter& writer) {
        writer.beginObject();
        writer.member("status", "error");
        writer.member("sample", "tina_sample_ui_showcase");
        writer.member("domain", static_cast<Core::u16>(3U));
        writer.member("code", static_cast<Core::u32>(7U));
        writer.member("message", "could not open \"file\"");
        writer.endObject();
    });

    EXPECT_EQ(json,
              R"({"status":"error","sample":"tina_sample_ui_showcase","domain":3,"code":7)"
              R"(,"message":"could not open \"file\""})");
}

} // namespace
} // namespace Tina::Tests
