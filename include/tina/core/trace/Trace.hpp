#pragma once

#if !defined(TINA_TRACE_BACKEND_NONE)
#error "Tina trace backend selection is missing"
#elif TINA_TRACE_BACKEND_NONE != 1
#error "TINA_TRACE_BACKEND_NONE must be defined as 1"
#endif

#define TINA_TRACE_ZONE(nameLiteral) \
    do {                              \
    } while (false)
