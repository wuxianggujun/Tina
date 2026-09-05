#include <tina/core/text/JsonDocument.hpp>

#include <span>
#include <string_view>
#include <type_traits>

static_assert(std::is_copy_constructible_v<Tina::Core::JsonDocument>);
static_assert(std::is_move_constructible_v<Tina::Core::JsonDocument>);
static_assert(noexcept(Tina::Core::JsonDocument{}.root()));
static_assert(noexcept(Tina::Core::JsonValue{}.kind()));
static_assert(noexcept(Tina::Core::JsonValue{}.size()));
static_assert(noexcept(Tina::Core::JsonValue{}.contains(std::string_view{})));
static_assert(std::is_same_v<
              decltype(Tina::Core::JsonDocument::parse(std::string_view{})),
              Tina::Core::Result<Tina::Core::JsonDocument>>);
