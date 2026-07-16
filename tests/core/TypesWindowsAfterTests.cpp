#include <tina/core/base/Types.hpp>

#if defined(_WIN32)
#include <Windows.h>
#endif

static_assert(sizeof(Tina::u32) == 4U);

#if defined(_WIN32)
static_assert(MAX_PATH > 0);
#endif
