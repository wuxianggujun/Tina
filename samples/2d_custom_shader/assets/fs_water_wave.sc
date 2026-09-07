$input v_texcoord0, v_color0, v_worldPos

#include <tina_sprite2d.sh>
#include <tina_water_wave.sh>

void main()
{
    vec4 base = texture2D(s_tex, v_texcoord0) * v_color0;
    float height = tinaWaterWaveHeight2D(v_worldPos, u_waterSurfaceParams.z);
    vec2 uv = v_texcoord0 + vec2(height * 0.08, height * 0.04);
    vec4 water = texture2D(s_tex, uv) * v_color0;
    float opacity = clamp(u_waterSurfaceParams.x, 0.0, 1.0);
    float ripple = clamp(abs(height) * 20.0 * (1.0 + u_waterSurfaceParams.y), 0.0, 1.0);
    vec3 tint = mix(water.rgb, water.rgb + vec3(0.08, 0.22, 0.32) * ripple, 0.75);
    gl_FragColor = vec4(tint * water.a, water.a * opacity);
}
