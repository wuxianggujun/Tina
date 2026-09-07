$input v_color0, v_texcoord0, v_normal, v_worldPos, v_tangent

// The varying line above must be the first line of the file: shaderc scans $input off the raw
// text before the preprocessor runs, so it cannot come from the include below.
//
// tina_mesh3d.sh declares the engine-owned sampler and uniform set. Including it is mandatory:
// bgfx dedupes uniforms by name, so re-declaring one of those with a different type would corrupt
// every engine draw that reads it. With the header in, shaderc reports a redefinition instead.
#include <tina_mesh3d.sh>
#include <tina_water_wave.sh>

// Author-declared uniform. Names are how the engine matches a value to a uniform, because the
// cooked binary's uniform order is a shaderc detail. vec4 is the only shape available.
//   x = tint mix, 0 = red band, 1 = green band
//   y = world-Y stripe frequency in cycles per metre
//   z = normal-visualisation blend
//   w = unused; the device writes 0 when the caller omits this uniform, which is the leak check
uniform vec4 u_tint;

void main()
{
    vec4 base = texture2D(s_texColor, v_texcoord0) * v_color0;
    vec3 n = normalize(v_normal);
    vec3 waterNormal = tinaWaterWaveNormal3D(v_worldPos);
    vec3 t = normalize(v_tangent.xyz);

    float stripeFreq = max(u_tint.y, 0.0001);
    float stripe = 0.5 + 0.5 * sin(v_worldPos.y * stripeFreq * 6.2831853);
    vec3 redTint = vec3(1.0, 0.12, 0.08);
    vec3 greenTint = vec3(0.08, 1.0, 0.16);
    vec3 tint = mix(redTint, greenTint, clamp(u_tint.x, 0.0, 1.0));

    vec3 color = base.rgb * tint * (0.70 + 0.30 * stripe);
    color = mix(color, abs(n), clamp(u_tint.z, 0.0, 1.0));
    float waterCrest = clamp(0.5 + 0.5 * tinaWaterWaveHeight3D(v_worldPos) /
                             max(abs(u_waterWave3D.z), 0.0001), 0.0, 1.0);
    color = mix(color, color + vec3(0.04, 0.16, 0.24), waterCrest * 0.35);
    color *= 0.75 + 0.25 * max(dot(n, waterNormal), 0.0);
    // Metallic darkens the tint when a material actually bound a non-zero factor. The sample
    // binds metallic 0, so this is a live read of the engine contract rather than a colour knob.
    color *= (1.0 - 0.25 * clamp(u_mrParams.x, 0.0, 1.0));
    color += t * 0.0;
    color += vec3(u_tint.w, 0.0, u_tint.w);

    gl_FragColor = vec4(color, 1.0);
}
