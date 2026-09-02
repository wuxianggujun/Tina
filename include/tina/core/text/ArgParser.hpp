#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/text/ParseInteger.hpp>

#include <charconv>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace Tina::Core {

// argv scanner for the engine tools and samples. Recognizes both --name=value and --name value for
// every option, so a caller does not have to pick one and users do not have to remember which tool
// wants which.
//
// Before this existed, each tool hand-rolled its own loop and the two spellings had split along
// tool boundaries: tina_assetc and tina_catalog_validate accepted only --name value, while
// tina_bench and the editor gate accepted only --name=value. Passing the wrong one produced
// "unknown argument", not a hint.
//
// Duplicate detection is deliberately NOT done here. Some options are meant to repeat --
// tina_assetc appends every --recipe and --gltf to a list, tina_catalog_validate every --asset-id --
// and a scanner cannot know which. Callers that need at-most-once track it themselves; the editor
// gate's assignOnce is the existing example.
//
// Nothing here allocates or copies: every view points into argv, which outlives main.
class ArgScanner final {
  public:
    ArgScanner(int argc, char** argv) noexcept
        : argv_(argv), argc_(argc)
    {
    }

    // Advances to the next token. Call once per loop iteration, before any matching.
    [[nodiscard]] bool next() noexcept
    {
        if (index_ + 1 >= argc_)
        {
            return false;
        }
        ++index_;
        token_ = argv_[index_];
        return true;
    }

    // The token as given, for the caller's "unknown argument" message.
    [[nodiscard]] std::string_view token() const noexcept
    {
        return token_;
    }

    // True when the current token is exactly name. For options that take no value.
    [[nodiscard]] bool flag(std::string_view name) const noexcept
    {
        return token_ == name;
    }

    // Returns the value when the current token is name=<value> or name followed by a separate
    // value token, and nullopt when the token is some other option.
    //
    // A token that IS name but has no value available is a distinct outcome from "not this option":
    // it returns nullopt and latches failedOption(). A caller that treated the two alike would
    // report a missing value as an unknown argument. Check failedOption() once, immediately before
    // the unknown-argument branch -- the flag stays latched, so one check covers every option in
    // the loop.
    //
    // An empty value is a value. The tools this replaces used an empty string_view as their
    // missing-value signal, so `--out ""` reported "missing value for --out".
    [[nodiscard]] std::optional<std::string_view> value(std::string_view name) noexcept
    {
        if (token_.starts_with(name))
        {
            const std::string_view remainder = token_.substr(name.size());
            if (remainder.empty())
            {
                if (index_ + 1 >= argc_)
                {
                    failedOption_ = name;
                    return std::nullopt;
                }
                ++index_;
                return std::string_view{argv_[index_]};
            }
            if (remainder.front() == '=')
            {
                return remainder.substr(1U);
            }
            // A longer option that merely shares this prefix, e.g. --out-of-line against --out.
            // Falling through lets the caller's later, longer test claim it.
        }
        return std::nullopt;
    }

    // Set once an option appeared without its value; names that option. Stays set.
    [[nodiscard]] std::string_view failedOption() const noexcept
    {
        return failedOption_;
    }

    [[nodiscard]] bool failed() const noexcept
    {
        return !failedOption_.empty();
    }

  private:
    char** argv_ = nullptr;
    int argc_ = 0;
    int index_ = 0;
    std::string_view token_{};
    std::string_view failedOption_{};
};

// Parses an unsigned option value; forwards to parseUnsigned.
//
// This exists so call sites can use a name that signals "command-line argument" rather than
// "generic text", but the implementation is the shared parseUnsigned from ParseInteger.hpp.
template <typename Value>
[[nodiscard]] bool parseArgUnsigned(std::string_view text, Value& out) noexcept
{
    return parseUnsigned(text, out);
}

} // namespace Tina::Core
