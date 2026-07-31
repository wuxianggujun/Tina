$input v_texcoord0, v_color0, v_worldPos

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);

// xy = world position, z = radius in meters, w = active slot.
uniform vec4 u_spriteLightPosRadius[8];
// rgb = linear light color * intensity, w unused.
uniform vec4 u_spriteLightColors[8];
// xy = segment start, zw = segment end. Unused slots are degenerate zero segments.
uniform vec4 u_spriteShadowSegments[32];
// x = ambient scale, y = active point-light count, z = active shadow-segment count.
uniform vec4 u_spriteLightParams;

float cross2D(vec2 left, vec2 right)
{
    return left.x * right.y - left.y * right.x;
}

bool shadowSegmentBlocksLight(vec2 fragmentPosition, vec2 lightPosition, vec4 segment)
{
    vec2 lightRay = lightPosition - fragmentPosition;
    vec2 segmentStart = segment.xy;
    vec2 segmentDirection = segment.zw - segmentStart;
    float denominator = cross2D(lightRay, segmentDirection);
    if (abs(denominator) < 0.00001)
    {
        return false;
    }
    vec2 offset = segmentStart - fragmentPosition;
    float lightRayFactor = cross2D(offset, segmentDirection) / denominator;
    float segmentFactor = cross2D(offset, lightRay) / denominator;
    return lightRayFactor > 0.001 && lightRayFactor < 0.999 &&
           segmentFactor >= 0.0 && segmentFactor <= 1.0;
}

void main()
{
    // Sample product/fixture texture and modulate by per-vertex color.
    // Premultiply alpha for the Sprite2D blend state (ONE, INV_SRC_ALPHA).
    vec4 tex = texture2D(s_tex, v_texcoord0);
    vec4 color = tex * v_color0;
    vec3 lighting = vec3_splat(max(u_spriteLightParams.x, 0.0));
    for (int lightIndex = 0; lightIndex < 8; ++lightIndex)
    {
        if (u_spriteLightPosRadius[lightIndex].w > 0.5)
        {
            vec3 pointLight = u_spriteLightPosRadius[lightIndex].xyz;
            float distanceToLight = length(pointLight.xy - v_worldPos);
            float attenuation = max(1.0 - distanceToLight / pointLight.z, 0.0);
            if (attenuation > 0.0)
            {
                for (int segmentIndex = 0; segmentIndex < 32; ++segmentIndex)
                {
                    if (float(segmentIndex) < u_spriteLightParams.z &&
                        shadowSegmentBlocksLight(
                            v_worldPos,
                            pointLight.xy,
                            u_spriteShadowSegments[segmentIndex]))
                    {
                        attenuation = 0.0;
                        break;
                    }
                }
            }
            lighting += u_spriteLightColors[lightIndex].rgb * attenuation;
        }
    }
    gl_FragColor = vec4(color.rgb * lighting * color.a, color.a);
}
