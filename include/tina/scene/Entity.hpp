#pragma once

#include <tina/core/id/GenerationId.hpp>

namespace Tina::Scene {

namespace Detail {
struct EntityRegistryTag final {
};
} // namespace Detail

// Runtime-only identity. The owner and generation are validated by World on
// every access; callers cannot manufacture a valid entity from an index.
using EntityId = Core::GenerationId<Detail::EntityRegistryTag>;

} // namespace Tina::Scene
