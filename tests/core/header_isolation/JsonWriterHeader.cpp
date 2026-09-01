#include <tina/core/text/JsonWriter.hpp>

#include <ostream>
#include <string_view>
#include <type_traits>

static_assert(Tina::Core::JsonWriter::MaximumDepth == 16U);

// Every writing operation is noexcept: these run on the failure path of samples and gates, where
// an exception would replace the diagnostic with a crash.
static_assert(noexcept(Tina::Core::JsonWriter(*static_cast<std::ostream*>(nullptr))));
static_assert(noexcept(static_cast<Tina::Core::JsonWriter*>(nullptr)->beginObject()));
static_assert(noexcept(static_cast<Tina::Core::JsonWriter*>(nullptr)->endObject()));
static_assert(noexcept(static_cast<Tina::Core::JsonWriter*>(nullptr)->beginArray()));
static_assert(noexcept(static_cast<Tina::Core::JsonWriter*>(nullptr)->endArray()));
static_assert(
    noexcept(static_cast<Tina::Core::JsonWriter*>(nullptr)->member(std::string_view{}, 0U)));
static_assert(noexcept(static_cast<Tina::Core::JsonWriter*>(nullptr)->balanced()));

// Non-copyable and non-movable: two writers on one stream would each keep their own comma state
// and interleave into malformed output.
static_assert(!std::is_copy_constructible_v<Tina::Core::JsonWriter>);
static_assert(!std::is_copy_assignable_v<Tina::Core::JsonWriter>);
static_assert(!std::is_move_constructible_v<Tina::Core::JsonWriter>);
static_assert(!std::is_move_assignable_v<Tina::Core::JsonWriter>);

// A char or const char* member must select the quoted string overload, never the numeric one --
// otherwise a name would silently be emitted as a bare integer.
static_assert(std::is_same_v<decltype(static_cast<Tina::Core::JsonWriter*>(nullptr)->member(
                                 std::string_view{}, "text")),
                             void>);
