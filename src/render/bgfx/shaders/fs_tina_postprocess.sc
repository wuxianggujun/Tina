$input v_texcoord0
#include <tina_postprocess.sh>

vec3 linearToSrgb(vec3 value)
{
    value = clamp(value, vec3_splat(0.0), vec3_splat(1.0));
    return mix(value * 12.92, 1.055 * pow(value, vec3_splat(1.0 / 2.4)) - 0.055,
               step(vec3_splat(0.0031308), value));
}
vec3 toneMap(vec3 color)
{
    vec3 value = min(max(color, vec3_splat(0.0)) * u_postParams.z, vec3_splat(65504.0));
    float operation = u_postParams.y;
    if (operation < 0.5) return value;
    if (operation < 1.5) return value / (vec3_splat(1.0) + value);
    if (operation < 2.5)
        return clamp((value * (2.51 * value + 0.03)) /
                     (value * (2.43 * value + 0.59) + 0.14), vec3_splat(0.0), vec3_splat(1.0));
    vec3 x = value / (value + 0.6);
    return x * x * (3.0 - 2.0 * x);
}
vec4 tentFilter(vec2 uv)
{
    vec2 texel = u_postSourceInfo.xy;
    vec4 sum = tinaPostSample(uv) * 4.0;
    sum += tinaPostSample(uv + vec2(-texel.x, 0.0)) * 2.0;
    sum += tinaPostSample(uv + vec2( texel.x, 0.0)) * 2.0;
    sum += tinaPostSample(uv + vec2(0.0, -texel.y)) * 2.0;
    sum += tinaPostSample(uv + vec2(0.0,  texel.y)) * 2.0;
    sum += tinaPostSample(uv + vec2(-texel.x, -texel.y));
    sum += tinaPostSample(uv + vec2( texel.x, -texel.y));
    sum += tinaPostSample(uv + vec2(-texel.x,  texel.y));
    sum += tinaPostSample(uv + vec2( texel.x,  texel.y));
    return sum * (1.0 / 16.0);
}
vec4 downsample(vec2 uv)
{
    vec2 offset = u_postSourceInfo.xy * 0.5;
    return (tinaPostSample(uv + vec2(-offset.x, -offset.y)) +
            tinaPostSample(uv + vec2( offset.x, -offset.y)) +
            tinaPostSample(uv + vec2(-offset.x,  offset.y)) +
            tinaPostSample(uv + vec2( offset.x,  offset.y))) * 0.25;
}
vec3 bloomExtract(vec3 value)
{
    value = max(value, vec3_splat(0.0));
    float brightness = max(max(value.r, value.g), value.b);
    float knee = max(u_postBloom.x * u_postBloom.y, 0.00001);
    float contribution = clamp(brightness - u_postBloom.x + knee, 0.0, 2.0 * knee);
    contribution = contribution * contribution / (4.0 * knee + 0.00001);
    contribution = max(contribution, brightness - u_postBloom.x) / max(brightness, 0.00001);
    return value * contribution;
}
void main()
{
    vec2 uv = v_texcoord0;
    float kind = u_postParams.x;
    // Decal/Fog read depth only; destination color is read by fixed-function
    // blending, never rebound as a sampler on its own render pass.
    if (kind < 2.5)
    {
        float depth = tinaPostSample(uv).r;
        vec2 viewportUv = (uv - u_postViewport.xy) / u_postViewport.zw;
        if (depth >= 0.999999 || any(lessThan(viewportUv, vec2_splat(0.0))) ||
            any(greaterThan(viewportUv, vec2_splat(1.0))))
        {
            gl_FragColor = vec4_splat(0.0);
            return;
        }
        float clipDepth = mix(depth, depth * 2.0 - 1.0, u_postCamera.w);
        vec4 world = mul(u_postInverseViewProjection,
                         vec4(viewportUv.x * 2.0 - 1.0, 1.0 - viewportUv.y * 2.0, clipDepth, 1.0));
        world.xyz /= world.w;
        if (kind < 1.5)
        {
            vec4 local = mul(u_postDecalFromWorld, vec4(world.xyz, 1.0));
            if (any(greaterThan(abs(local.xyz), vec3_splat(0.5))))
            {
                gl_FragColor = vec4_splat(0.0);
                return;
            }
            // XY projection of the decal unit box; asset textures have their
            // authored UV orientation, not a framebuffer origin.
            vec4 decal = texture2D(s_postAuxiliary, vec2(local.x + 0.5, 0.5 - local.y));
            gl_FragColor = decal * u_postDecalColor;
            return;
        }
        float distance = length(world.xyz - u_postCamera.xyz);
        float visibility;
        if (u_postFog.x < 0.5)
            visibility = (u_postFog.w - distance) / max(u_postFog.w - u_postFog.z, 0.00001);
        else if (u_postFog.x < 1.5)
            visibility = exp(-u_postFog.y * distance);
        else
        {
            float factor = u_postFog.y * distance;
            visibility = exp(-factor * factor);
        }
        gl_FragColor = vec4(u_postFogColor.rgb, 1.0 - clamp(visibility, 0.0, 1.0));
        return;
    }
    if (kind < 3.5) { gl_FragColor = vec4(bloomExtract(downsample(uv).rgb), 1.0); return; }
    if (kind < 4.5) { gl_FragColor = vec4(downsample(uv).rgb, 1.0); return; }
    if (kind < 5.5) { gl_FragColor = vec4(tentFilter(uv).rgb, 1.0); return; }
    if (kind < 6.5)
    {
        // Normalized reconstruction: constant radiance stays constant as the
        // mip count changes instead of gaining brightness once per level.
        gl_FragColor = vec4(mix(tinaPostSampleAuxiliary(uv).rgb, tentFilter(uv).rgb, 0.5), 1.0);
        return;
    }
    if (kind < 7.5)
    {
        vec4 source = tinaPostSample(uv);
        gl_FragColor = vec4(linearToSrgb(toneMap(source.rgb)), source.a);
        return;
    }
    if (kind > 10.5) { gl_FragColor = vec4(tentFilter(uv).rgb * u_postBloom.z, 0.0); return; }
    gl_FragColor = tinaPostSample(uv);
}
