#ifndef TINA_WATER_WAVE_SH_HEADER_GUARD
#define TINA_WATER_WAVE_SH_HEADER_GUARD

#include <bgfx_shader.sh>

// x/y = direction A, z = amplitude, w = wavelength; second row is wave B.
uniform vec4 u_waterWave2DA;
uniform vec4 u_waterWave2DB;
// x/y = speed/phase for A, z/w = speed/phase for B.
uniform vec4 u_waterWave2DTime;
// x = opacity, y = ripple strength, z/w reserved.
uniform vec4 u_waterSurfaceParams;
// x/y = direction on XZ, z = amplitude, w = wavelength.
uniform vec4 u_waterWave3D;
// x = speed, y = phase, z/w reserved.
uniform vec4 u_waterWave3DTime;

float tinaWaterWaveHeight2D(vec2 position, vec4 wave, vec4 timeAndPhase, float timeSeconds, bool secondWave)
{
    vec2 direction = wave.xy;
    float directionLength = max(length(direction), 0.00001);
    float projected = dot(position, direction / directionLength);
    float phase = secondWave ? timeAndPhase.z * timeSeconds + timeAndPhase.w
                             : timeAndPhase.x * timeSeconds + timeAndPhase.y;
    return wave.z * sin(6.2831853 * projected / max(wave.w, 0.00001) + phase);
}

float tinaWaterWaveHeight2D(vec2 position, float timeSeconds)
{
    return tinaWaterWaveHeight2D(position, u_waterWave2DA, u_waterWave2DTime, timeSeconds, false) +
           tinaWaterWaveHeight2D(position, u_waterWave2DB, u_waterWave2DTime, timeSeconds, true);
}

float tinaWaterWaveHeight3D(vec3 position)
{
    vec2 direction = u_waterWave3D.xy;
    float directionLength = max(length(direction), 0.00001);
    float projected = dot(position.xz, direction / directionLength);
    return u_waterWave3D.z * sin(6.2831853 * projected / max(u_waterWave3D.w, 0.00001) +
                                 u_waterWave3DTime.x * u_waterWave3DTime.z + u_waterWave3DTime.y);
}

vec3 tinaWaterWaveNormal3D(vec3 position)
{
    vec2 direction = normalize(u_waterWave3D.xy);
    float frequency = 6.2831853 / max(u_waterWave3D.w, 0.00001);
    float slope = u_waterWave3D.z * frequency *
                  cos(frequency * dot(position.xz, direction) +
                      u_waterWave3DTime.x * u_waterWave3DTime.z + u_waterWave3DTime.y);
    return normalize(vec3(-direction.x * slope, 1.0, -direction.y * slope));
}

#endif
