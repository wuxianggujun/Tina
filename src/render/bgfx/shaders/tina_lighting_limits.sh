#ifndef TINA_LIGHTING_LIMITS_SH
#define TINA_LIGHTING_LIMITS_SH

// One contract shared by C++ uploads and every engine/custom fragment shader.
// These are GPU uniform slots, not CPU scene/container capacities.
#define TINA_DIRECTIONAL_LIGHT_SLOTS 4
#define TINA_POINT_LIGHT_SLOTS 8
#define TINA_SPOT_LIGHT_SLOTS 8
#define TINA_SPRITE_POINT_LIGHT_SLOTS 8
#define TINA_SPRITE_SHADOW_SEGMENT_SLOTS 32

#endif
