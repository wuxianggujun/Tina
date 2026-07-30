$input v_color0, v_texcoord0, v_shapeParams

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    // R8 atlas pages and the solid 1x1 white page both sample .r as coverage.
    float coverage = texture2D(s_texColor, v_texcoord0).r;
    float radius = v_shapeParams.z;
    if (radius > 0.0)
    {
        vec2 halfExtent = v_shapeParams.xy * 0.5;
        vec2 localPoint = (v_texcoord0 - vec2(0.5, 0.5)) * v_shapeParams.xy;
        vec2 outside = abs(localPoint) - (halfExtent - vec2(radius, radius));
        float distance = length(max(outside, vec2(0.0, 0.0)))
                       + min(max(outside.x, outside.y), 0.0) - radius;
        coverage *= clamp(0.5 - distance, 0.0, 1.0);
    }
    gl_FragColor = v_color0 * coverage;
}
