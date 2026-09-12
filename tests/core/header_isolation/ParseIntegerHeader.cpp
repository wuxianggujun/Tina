#include <tina/core/text/ParseInteger.hpp>

namespace {
[[maybe_unused]] bool parseIntegerFromIsolatedHeader(std::string_view text)
{
    Tina::Core::u32 unsignedValue = 0;
    Tina::Core::i64 signedValue = 0;
    static_assert(noexcept(Tina::Core::parseUnsigned(text, unsignedValue)));
    static_assert(noexcept(Tina::Core::parseSigned(text, signedValue)));
    return Tina::Core::parseUnsigned(text, unsignedValue) &&
           Tina::Core::parseSigned(text, signedValue);
}
}
