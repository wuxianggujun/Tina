#include <tina/core/text/ParseFloat.hpp>

#include <optional>
#include <type_traits>

static_assert(std::is_same_v<
              decltype(Tina::Core::parseStrictFloat(std::string_view{})),
              std::optional<float>>);
static_assert(noexcept(Tina::Core::parseStrictFloat(std::string_view{})));
static_assert(Tina::Core::MaximumParsedFloatBytes == 63U);
