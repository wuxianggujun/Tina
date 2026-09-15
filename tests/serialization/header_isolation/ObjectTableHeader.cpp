#include <tina/serialization/ObjectTable.hpp>

static_assert(Tina::Serialization::ObjectId{}.value == 0);
namespace {
struct Root { virtual ~Root() = default; };
static_assert(!std::is_copy_constructible_v<Tina::Serialization::ObjectTable<Root>>);
static_assert(std::is_nothrow_move_constructible_v<Tina::Serialization::ObjectTable<Root>>);
}
