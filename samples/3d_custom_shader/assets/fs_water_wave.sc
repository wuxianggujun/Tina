$input v_color0, v_texcoord0, v_normal, v_worldPos, v_tangent

#include <tina_mesh3d.sh>
#include <tina_water_wave.sh>

vec4 tinaMesh3DFragment(vec4 base, vec2 texcoord0, vec3 surfaceNormal,
    vec3 worldPosition, vec4 surfaceTangent, float frontFaceSign)
{
    vec3 waveNormal = tinaWaterWaveNormal3D(worldPosition);
    float crest = clamp(0.5 + 0.5 * tinaWaterWaveHeight3D(worldPosition) / max(abs(u_waterWave3D.z), 0.0001), 0.0, 1.0);
    vec3 color = mix(base.rgb, base.rgb + vec3(0.05, 0.18, 0.25), crest * 0.6);
    color *= 0.65 + 0.35 * max(dot(normalize(surfaceNormal) * frontFaceSign, waveNormal), 0.0);
    color += vec3(texcoord0, surfaceTangent.w) * 0.0;
    return vec4(color, base.a);
}
