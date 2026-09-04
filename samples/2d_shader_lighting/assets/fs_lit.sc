$input v_texcoord0, v_color0, v_worldPos

// The varying line above must be the first line of the file: shaderc scans $input off the raw text
// before the preprocessor runs, so it cannot come from the include below.
//
// tina_sprite2d.sh declares the engine-owned samplers and lighting uniforms. Including it is
// mandatory: bgfx dedupes uniforms by name, so re-declaring one of those with a different type
// would corrupt every engine draw that reads it. With the header in, shaderc reports a
// redefinition instead.
#include <tina_sprite2d.sh>

// Custom fragment that *consumes* the engine lighting/normal contract rather than replacing it.
// The helpers below are the same formulae as fs_tina_sprite2d_fixture.sc; they live here rather
// than in the contract header because a material that wants a different lighting model must be
// free to omit them.

vec3 safeNormalize(vec3 value)
{
    return value * inversesqrt(max(dot(value, value), 0.00000001));
}

vec3 mappedSpriteNormal(vec2 worldPosition, vec2 textureCoordinates)
{
    vec3 geometricNormal = vec3(0.0, 0.0, 1.0);
    vec3 worldDx = vec3(dFdx(worldPosition), 0.0);
    vec3 worldDy = vec3(dFdy(worldPosition), 0.0);
    vec2 uvDx = dFdx(textureCoordinates);
    vec2 uvDy = dFdy(textureCoordinates);

    vec3 worldDyPerpendicular = cross(worldDy, geometricNormal);
    vec3 worldDxPerpendicular = cross(geometricNormal, worldDx);
    vec3 tangent = worldDyPerpendicular * uvDx.x + worldDxPerpendicular * uvDy.x;
    vec3 bitangent = worldDyPerpendicular * uvDx.y + worldDxPerpendicular * uvDy.y;
    float inverseBasisScale = inversesqrt(
        max(max(dot(tangent, tangent), dot(bitangent, bitangent)), 0.00000001));
    tangent *= inverseBasisScale;
    bitangent *= inverseBasisScale;

    vec3 normalSample = texture2D(s_normalTex, textureCoordinates).xyz;
    vec2 mapXY = clamp((normalSample.xy * 255.0 - 128.0) / 127.0, -1.0, 1.0);
    vec3 mapNormal = vec3(mapXY, normalSample.z * 2.0 - 1.0);
    return safeNormalize(
        tangent * mapNormal.x + bitangent * mapNormal.y + geometricNormal * mapNormal.z);
}

float relativePointLightLambert(vec3 surfaceNormal, vec2 lightOffset, float lightRadius)
{
    vec2 relativePlanarDirection = lightOffset / lightRadius;
    return max(surfaceNormal.z + dot(surfaceNormal.xy, relativePlanarDirection), 0.0);
}

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
    vec4 color = texture2D(s_tex, v_texcoord0) * v_color0;
    bool normalMapEnabled = u_spriteNormalParams.x > 0.5;
    vec3 surfaceNormal = vec3(0.0, 0.0, 1.0);
    if (normalMapEnabled)
    {
        surfaceNormal = mappedSpriteNormal(v_worldPos, v_texcoord0);
    }

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
                float shadowVisibility = 1.0;
                for (int segmentIndex = 0; segmentIndex < 32; ++segmentIndex)
                {
                    if (float(segmentIndex) < u_spriteLightParams.z)
                    {
                        if (shadowSegmentBlocksLight(
                                v_worldPos, pointLight.xy, u_spriteShadowSegments[segmentIndex]))
                        {
                            shadowVisibility = 0.0;
                            break;
                        }
                    }
                }
                attenuation *= shadowVisibility;
            }
            if (normalMapEnabled)
            {
                attenuation *= relativePointLightLambert(
                    surfaceNormal, pointLight.xy - v_worldPos, pointLight.z);
            }
            lighting += u_spriteLightColors[lightIndex].rgb * attenuation;
        }
    }

    // Premultiplied alpha: the Sprite2D batch blend state is (ONE, INV_SRC_ALPHA).
    gl_FragColor = vec4(color.rgb * lighting * color.a, color.a);
}
