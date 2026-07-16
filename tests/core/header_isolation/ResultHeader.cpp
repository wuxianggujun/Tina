#include <tina/core/error/Result.hpp>

#include <expected>
#include <type_traits>

static_assert(std::is_same_v<
              Tina::Core::Result<int>,
              std::expected<int, Tina::Core::Error>>);
