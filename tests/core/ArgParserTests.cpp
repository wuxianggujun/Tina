#include <tina/core/text/ArgParser.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace Tina::Tests {
namespace {

// Owns the argv storage so the string_views the scanner hands back stay valid for the test body.
class Argv final {
  public:
    explicit Argv(const std::vector<std::string>& tokens)
    {
        storage_.reserve(tokens.size() + 1U);
        storage_.emplace_back("tool.exe");
        storage_.insert(storage_.end(), tokens.begin(), tokens.end());
        for (auto& token : storage_)
        {
            pointers_.push_back(token.data());
        }
    }

    [[nodiscard]] Core::ArgScanner scanner() noexcept
    {
        return Core::ArgScanner(static_cast<int>(pointers_.size()), pointers_.data());
    }

  private:
    std::vector<std::string> storage_;
    std::vector<char*> pointers_;
};

// The whole point of the type: the two spellings that had split along tool boundaries both work.
TEST(ArgParserTest, AcceptsBothEqualsAndSpaceSpellings)
{
    for (const auto& tokens : {std::vector<std::string>{"--out=build/assets"},
                               std::vector<std::string>{"--out", "build/assets"}})
    {
        Argv argv(tokens);
        auto scanner = argv.scanner();
        std::string out;
        while (scanner.next())
        {
            if (const auto value = scanner.value("--out"))
            {
                out.assign(*value);
            }
        }
        EXPECT_FALSE(scanner.failed());
        EXPECT_EQ(out, "build/assets");
    }
}

// A longer option that shares a shorter one's prefix must not be swallowed by it. Real pair:
// tina_catalog_validate has both --max-dependencies and --max-dependencies-per-asset, and the
// shorter one is tested first.
TEST(ArgParserTest, LongerOptionSharingAPrefixIsNotSwallowed)
{
    Argv argv({std::string("--max-dependencies-per-asset=7")});
    auto scanner = argv.scanner();

    Core::u32 shorter = 0;
    Core::u32 longer = 0;
    while (scanner.next())
    {
        if (const auto value = scanner.value("--max-dependencies"))
        {
            ASSERT_TRUE(Core::parseArgUnsigned(*value, shorter));
            continue;
        }
        if (const auto value = scanner.value("--max-dependencies-per-asset"))
        {
            ASSERT_TRUE(Core::parseArgUnsigned(*value, longer));
        }
    }

    EXPECT_EQ(shorter, 0U) << "the shorter option consumed a token that was not its own";
    EXPECT_EQ(longer, 7U);
}

// The bug this fixes. Every tool being replaced used an empty string_view as its missing-value
// sentinel, so `--out ""` was rejected as "missing value for --out".
TEST(ArgParserTest, EmptyValueIsAValueNotAMissingValue)
{
    Argv argv({std::string("--out"), std::string("")});
    auto scanner = argv.scanner();

    bool matched = false;
    while (scanner.next())
    {
        if (const auto value = scanner.value("--out"))
        {
            matched = true;
            EXPECT_TRUE(value->empty());
        }
    }

    EXPECT_TRUE(matched);
    EXPECT_FALSE(scanner.failed()) << "an empty value was reported as a missing value";
}

// "not this option" and "this option, but its value is missing" are both nullopt, so the latch is
// the only thing that separates them. Without it a trailing --out would be reported as an unknown
// argument.
TEST(ArgParserTest, TrailingOptionWithNoValueLatchesTheOptionName)
{
    Argv argv({std::string("--out")});
    auto scanner = argv.scanner();

    while (scanner.next())
    {
        if (const auto value = scanner.value("--out"))
        {
            FAIL() << "a value appeared where argv had none";
        }
    }

    EXPECT_TRUE(scanner.failed());
    EXPECT_EQ(scanner.failedOption(), "--out");
}

// A value that looks like an option is still a value. `--suggested-name --out` sets the name to
// "--out"; that is what the space form means and the tools already behaved this way.
TEST(ArgParserTest, ValueBeginningWithDashesIsConsumedAsAValue)
{
    Argv argv({std::string("--suggested-name"), std::string("--out")});
    auto scanner = argv.scanner();

    std::string name;
    bool sawOut = false;
    while (scanner.next())
    {
        if (const auto value = scanner.value("--suggested-name"))
        {
            name.assign(*value);
            continue;
        }
        if (scanner.flag("--out"))
        {
            sawOut = true;
        }
    }

    EXPECT_EQ(name, "--out");
    EXPECT_FALSE(sawOut) << "the value token was scanned a second time as an option";
}

TEST(ArgParserTest, FlagMatchesOnlyTheExactToken)
{
    Argv argv({std::string("--metadata-only"), std::string("--metadata-only=1")});
    auto scanner = argv.scanner();

    int exact = 0;
    int inexact = 0;
    while (scanner.next())
    {
        if (scanner.flag("--metadata-only"))
        {
            ++exact;
            continue;
        }
        ++inexact;
    }

    EXPECT_EQ(exact, 1);
    EXPECT_EQ(inexact, 1) << "--metadata-only=1 must fall through to the unknown-argument branch";
}

TEST(ArgParserTest, UnsignedParsingRejectsAnythingTheWholeTextIsNot)
{
    Core::u32 value = 0;
    EXPECT_TRUE(Core::parseArgUnsigned("0", value));
    EXPECT_EQ(value, 0U);
    EXPECT_TRUE(Core::parseArgUnsigned("4294967295", value));
    EXPECT_EQ(value, 4294967295U);

    EXPECT_FALSE(Core::parseArgUnsigned("", value));
    EXPECT_FALSE(Core::parseArgUnsigned("12x", value));
    EXPECT_FALSE(Core::parseArgUnsigned(" 12", value));
    EXPECT_FALSE(Core::parseArgUnsigned("12 ", value));
    EXPECT_FALSE(Core::parseArgUnsigned("0x10", value));
    // from_chars takes no sign for unsigned targets, which is what the digit loops did too.
    EXPECT_FALSE(Core::parseArgUnsigned("+12", value));
    EXPECT_FALSE(Core::parseArgUnsigned("-12", value));
}

// The narrow target must reject what overflows it, not wrap. 4294967296 fits in the u64 that
// from_chars reads into, so only the explicit range check catches it.
TEST(ArgParserTest, UnsignedParsingRejectsValuesAboveTheTargetMaximum)
{
    Core::u32 narrow = 0;
    EXPECT_FALSE(Core::parseArgUnsigned("4294967296", narrow));
    EXPECT_EQ(narrow, 0U) << "a rejected value must leave the output untouched";

    Core::u64 wide = 0;
    EXPECT_TRUE(Core::parseArgUnsigned("4294967296", wide));
    EXPECT_EQ(wide, 4294967296ULL);
    EXPECT_FALSE(Core::parseArgUnsigned("18446744073709551616", wide));
}

TEST(ArgParserTest, ScannerStopsAtTheEndOfArgv)
{
    Argv argv({});
    auto scanner = argv.scanner();
    EXPECT_FALSE(scanner.next());
    EXPECT_FALSE(scanner.failed());
}

} // namespace
} // namespace Tina::Tests
