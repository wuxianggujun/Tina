#ifndef TINA_WATER_WAVE_SH_HEADER_GUARD
#define TINA_WATER_WAVE_SH_HEADER_GUARD

#include <bgfx_shader.sh>

// x/y = direction A, z = amplitude, w = wavelength; second row is wave B.
uniform vec4 u_waterWave2D[2];
// x/y = speed/phase for A, z/w = speed/phase for B.
uniform vec4 u_waterWave2DTime;
// x = opacity, y = ripple strength, z/w reserved.
uniform vec4 u_waterSurfaceParams;
// x/y = direction on XZ, z = amplitude, w = wavelength.
uniform vec4 u_waterWave3D;
// x = speed, y = phase, z/w reserved.
uniform vec4 u_waterWave3DTime;

#endif
