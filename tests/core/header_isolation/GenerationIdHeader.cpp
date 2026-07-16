#include <tina/core/id/GenerationId.hpp>

struct GenerationIdHeaderTag;
static_assert(!Tina::Core::GenerationId<GenerationIdHeaderTag>{}.hasValue());
