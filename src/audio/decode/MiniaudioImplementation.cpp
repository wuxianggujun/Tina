// One implementation for decoding and the optional device adapter in Tina.lib.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

static_assert(MA_VERSION_MAJOR == 0 && MA_VERSION_MINOR == 11 && MA_VERSION_REVISION == 25,
              "Review and update the pinned custom decoder adapters with miniaudio");
