#if defined(_WIN32)
#include <Windows.h>
#endif

#include "core/base/Types.hpp"

static_assert(Tina::Core::MaxPathLength == 260U);

#if defined(_WIN32)
static_assert(MAX_PATH > 0);
#endif
