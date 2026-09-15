#include <tina/serialization/TypeRegistry.hpp>

namespace {
struct Root { virtual ~Root() = default; };
[[maybe_unused]] Tina::Serialization::TypeRegistry<Root> registry;
}
