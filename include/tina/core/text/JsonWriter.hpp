#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/diagnostics/Assert.hpp>

#include <array>
#include <ostream>
#include <string_view>
#include <type_traits>

namespace Tina::Core {

// Compact single-line JSON writer for the diagnostic reports that samples, the editor and the
// bench tools print on stdout/stderr. Write-only by design: there is no parser and no DOM here,
// because the only JSON *reading* in C++ is glTF, which cgltf's bundled jsmn already does
// (ADR 0009). See ADR 0038 for why no third-party JSON library is used.
//
// What this type buys over streaming string literals: commas, quotes and brace/bracket pairing
// stop being the caller's problem. A missing comma in a hand-written chain compiles fine and only
// shows up as a gate that cannot find its evidence, with no hint about the real cause.
//
// The output format is a hard contract with the gates that consume it, so three properties are
// deliberate and must not be "improved":
//
//   1. Keys come out in insertion order. Six gates select their line with an anchored regex that
//      requires "status" first and "sample" second, adjacent -- for example
//      RunProduct2dGate.ps1:325. Sorting keys would break them at runtime, not at build time.
//   2. Output is compact: no space after ':' or ','. Around 130 bare-key patterns in
//      RunProduct2dGate.ps1:148-280 are spelled 'evidenceSchema\":29' and would stop matching.
//   3. One object per line; the caller writes the newline after end().
//
// Numbers are formatted by the wrapped std::ostream rather than here, so migrating a call site
// from a raw `<<` chain to this type cannot change the digits it prints.
class JsonWriter
{
public:
    // Deepest object/array nesting this tracks. The reports in this repo reach 4; the bound only
    // exists so the pairing check needs no allocation.
    static constexpr usize MaximumDepth = 16;

    explicit JsonWriter(std::ostream& output) noexcept : output_(&output) {}

    JsonWriter(const JsonWriter&) = delete;
    JsonWriter& operator=(const JsonWriter&) = delete;
    JsonWriter(JsonWriter&&) = delete;
    JsonWriter& operator=(JsonWriter&&) = delete;

    ~JsonWriter() = default;

    // --- structure -------------------------------------------------------------------------

    // Opens a scope, separated from a preceding sibling if there is one. At depth 0 the separator
    // is a no-op, so this is also the spelling for the outermost object.
    void beginObject() noexcept
    {
        writeSeparator();
        openScope('{');
    }

    void endObject() noexcept { endScope('{', '}'); }

    void beginArray() noexcept
    {
        writeSeparator();
        openScope('[');
    }

    void endArray() noexcept { endScope('[', ']'); }

    // --- members ---------------------------------------------------------------------------

    void member(const std::string_view key, const std::string_view value) noexcept
    {
        writeKey(key);
        writeEscaped(value);
    }

    // Needed even though string_view constructs from const char*: pointer-to-bool is a standard
    // conversion and beats the user-defined one, so without this a string literal would silently
    // be written as `true`.
    void member(const std::string_view key, const char* const value) noexcept
    {
        writeKey(key);
        writeEscaped(value != nullptr ? std::string_view(value) : std::string_view{});
    }

    // Constrained to exactly bool for the same reason: an unconstrained bool parameter swallows
    // every pointer.
    template <typename Value>
        requires std::is_same_v<std::remove_cv_t<Value>, bool>
    void member(const std::string_view key, const Value value) noexcept
    {
        writeKey(key);
        *output_ << (value ? "true" : "false");
    }

    // Arithmetic overload, minus bool (handled above) and minus the character types, so a
    // `const char*` or a `char` cannot silently be emitted as a bare number: those must go
    // through the string_view overload and be quoted.
    template <typename Value>
        requires(std::is_arithmetic_v<Value> && !std::is_same_v<std::remove_cv_t<Value>, bool> &&
                 !std::is_same_v<std::remove_cv_t<Value>, char> &&
                 !std::is_same_v<std::remove_cv_t<Value>, signed char> &&
                 !std::is_same_v<std::remove_cv_t<Value>, unsigned char>)
    void member(const std::string_view key, const Value value) noexcept
    {
        writeKey(key);
        // u8/i8 would print as a character through operator<<; the caller-visible overload set
        // excludes them, and everything else is already the type the old chains streamed.
        *output_ << value;
    }

    // Raw member for a value this type cannot spell -- currently only the pre-rendered numeric
    // text some reports build with std::to_chars. The text must be a complete, valid JSON value;
    // nothing about it is validated or escaped.
    void rawMember(const std::string_view key, const std::string_view jsonValueText) noexcept
    {
        writeKey(key);
        *output_ << jsonValueText;
    }

    // Opens a nested scope as the value of `key`. The caller closes it with endObject/endArray.
    // writeKey already emitted the separator, so the scope opens without one.
    void beginObjectMember(const std::string_view key) noexcept
    {
        writeKey(key);
        openScope('{');
    }

    void beginArrayMember(const std::string_view key) noexcept
    {
        writeKey(key);
        openScope('[');
    }

    // --- array elements --------------------------------------------------------------------

    void element(const std::string_view value) noexcept
    {
        writeSeparator();
        writeEscaped(value);
    }

    void element(const char* const value) noexcept
    {
        writeSeparator();
        writeEscaped(value != nullptr ? std::string_view(value) : std::string_view{});
    }

    template <typename Value>
        requires std::is_same_v<std::remove_cv_t<Value>, bool>
    void element(const Value value) noexcept
    {
        writeSeparator();
        *output_ << (value ? "true" : "false");
    }

    template <typename Value>
        requires(std::is_arithmetic_v<Value> && !std::is_same_v<std::remove_cv_t<Value>, bool> &&
                 !std::is_same_v<std::remove_cv_t<Value>, char> &&
                 !std::is_same_v<std::remove_cv_t<Value>, signed char> &&
                 !std::is_same_v<std::remove_cv_t<Value>, unsigned char>)
    void element(const Value value) noexcept
    {
        writeSeparator();
        *output_ << value;
    }

    // Same behaviour as beginObject/beginArray; named for the call site so that a scope opened as
    // an array element reads as one.
    void beginObjectElement() noexcept { beginObject(); }

    void beginArrayElement() noexcept { beginArray(); }

    // True once every opened scope has been closed. Worth asserting at the end of a report:
    // an unbalanced writer produces truncated JSON that a gate reports as a missing key.
    [[nodiscard]] bool balanced() const noexcept { return depth_ == 0U; }

    [[nodiscard]] usize depth() const noexcept { return depth_; }

    // Escapes `value` as a JSON string, including the surrounding quotes. Exposed because a few
    // call sites quote a lone string outside any object.
    void writeString(const std::string_view value) noexcept { writeEscaped(value); }

private:
    // Writes the opening brace/bracket and pushes the scope. Never emits a separator: the caller
    // has already done that, either directly or via writeKey. Emitting one here as well is what
    // produced doubled commas like "[,{...},,{...}]".
    void openScope(const char opening) noexcept
    {
        TINA_ASSERT(depth_ < MaximumDepth, "JsonWriter nesting exceeded MaximumDepth");
        if (depth_ >= MaximumDepth)
        {
            return;
        }
        openings_[depth_] = opening;
        hasMember_[depth_] = false;
        ++depth_;
        output_->put(opening);
    }

    void endScope(const char expectedOpening, const char closing) noexcept
    {
        TINA_ASSERT(depth_ > 0U, "JsonWriter closed a scope that was never opened");
        if (depth_ == 0U)
        {
            return;
        }
        --depth_;
        TINA_ASSERT(openings_[depth_] == expectedOpening,
                    "JsonWriter closed an object as an array or an array as an object");
        static_cast<void>(expectedOpening);
        output_->put(closing);
    }

    // Emits the comma that precedes every value except the first one in its scope. This is the
    // whole point of the type: the caller never spells a comma.
    void writeSeparator() noexcept
    {
        if (depth_ == 0U)
        {
            return;
        }
        if (hasMember_[depth_ - 1U])
        {
            output_->put(',');
        }
        hasMember_[depth_ - 1U] = true;
    }

    void writeKey(const std::string_view key) noexcept
    {
        TINA_ASSERT(depth_ > 0U && openings_[depth_ - 1U] == '{',
                    "JsonWriter wrote a named member outside an object");
        writeSeparator();
        writeEscaped(key);
        output_->put(':');
    }

    // Matches the most complete of the three escaping variants the 13 hand-copied
    // writeJsonString helpers had (ADR 0038); the other two silently dropped \r, \t and the
    // other control bytes, which is the behavior change that ADR records. Note what this
    // deliberately does not do: bytes >= 0x20 pass through unchanged, so invalid UTF-8 is
    // neither rejected nor replaced. Callers that need validity use Core::isStrictUtf8.
    void writeEscaped(const std::string_view value) noexcept
    {
        constexpr char Hexadecimal[] = "0123456789abcdef";
        output_->put('"');
        for (const unsigned char byte : value)
        {
            switch (byte)
            {
            case '"':
                *output_ << "\\\"";
                break;
            case '\\':
                *output_ << "\\\\";
                break;
            case '\b':
                *output_ << "\\b";
                break;
            case '\f':
                *output_ << "\\f";
                break;
            case '\n':
                *output_ << "\\n";
                break;
            case '\r':
                *output_ << "\\r";
                break;
            case '\t':
                *output_ << "\\t";
                break;
            default:
                if (byte < 0x20U)
                {
                    *output_ << "\\u00" << Hexadecimal[byte >> 4U] << Hexadecimal[byte & 0x0FU];
                }
                else
                {
                    output_->put(static_cast<char>(byte));
                }
                break;
            }
        }
        output_->put('"');
    }

    std::ostream* output_;
    std::array<char, MaximumDepth> openings_{};
    std::array<bool, MaximumDepth> hasMember_{};
    usize depth_ = 0U;
};

} // namespace Tina::Core
