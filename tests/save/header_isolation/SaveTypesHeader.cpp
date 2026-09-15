#include <tina/save/SaveTypes.hpp>

static_assert(Tina::Save::DefaultMaxSavePayloadBytes <=
              Tina::Save::MaxSavePayloadBytes);
