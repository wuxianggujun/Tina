$input v_color0, v_texcoord0, v_normal, v_worldPos, v_tangent

#include <tina_mesh3d.sh>
#include <tina_water_wave.sh>

void main()
{
    vec4 base = texture2D(s_texColor, v_texcoord0) * v_color0;
    vec3 waveNormal = tinaWaterWaveNormal3D(v_worldPos);
    float crest = clamp(0.5 + 0.5 * tinaWaterWaveHeight3D(v_worldPos) / max(abs(u_waterWave3D.z), 0.0001), 0.0, 1.0);
    vec3 color = mix(base.rgb, base.rgb + vec3(0.05, 0.18, 0.25), crest * 0.6);
    color *= 0.65 + 0.35 * max(dot(normalize(v_normal), waveNormal), 0.0);
    gl_FragColor = vec4(color, base.a);
}
