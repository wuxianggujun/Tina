#include <tina/core/trace/Trace.hpp>

#if defined(TINA_TRACE_BACKEND_NONE)
static_assert(TINA_TRACE_BACKEND_NONE == 1);
#elif defined(TINA_TRACE_BACKEND_ENABLED)
static_assert(TINA_TRACE_BACKEND_ENABLED == 1);
#else
#error "TraceHeader isolation requires a selected Tina trace backend"
#endif
