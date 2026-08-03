$input v_texcoord0, v_color0, v_worldPos

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);
SAMPLER2D(s_normalTex, 1);

// xy = world position, z = radius in meters, w = active slot.
uniform vec4 u_spriteLightPosRadius[8];
// rgb = linear light color * intensity, w = source radius in meters.
uniform vec4 u_spriteLightColors[8];
// xy = segment start, zw = segment end. Unused slots are degenerate zero segments.
uniform vec4 u_spriteShadowSegments[32];
// x = ambient scale, y = active point-light count, z = active shadow-segment count.
uniform vec4 u_spriteLightParams;
// x = 1 when the current (base texture, normal texture) batch has a live normal map.
uniform vec4 u_spriteNormalParams;

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

    // Cotangent frame follows the actual world/UV derivatives, so sprite
    // rotation, signed scale, atlas UVs, and flip flags need no extra branches.
    vec3 worldDyPerpendicular = cross(worldDy, geometricNormal);
    vec3 worldDxPerpendicular = cross(geometricNormal, worldDx);
    vec3 tangent = worldDyPerpendicular * uvDx.x + worldDxPerpendicular * uvDy.x;
    vec3 bitangent = worldDyPerpendicular * uvDx.y + worldDxPerpendicular * uvDy.y;
    float inverseBasisScale = inversesqrt(
        max(max(dot(tangent, tangent), dot(bitangent, bitangent)), 0.00000001));
    tangent *= inverseBasisScale;
    bitangent *= inverseBasisScale;

    vec3 normalSample = texture2D(s_normalTex, textureCoordinates).xyz;
    // RGBA8 normal maps conventionally encode neutral X/Y as 128. Remap that
    // texel exactly to zero so the device-owned flat map produces factor 1.
    vec2 mapXY = clamp((normalSample.xy * 255.0 - 128.0) / 127.0, -1.0, 1.0);
    vec3 mapNormal = vec3(mapXY, normalSample.z * 2.0 - 1.0);
    return safeNormalize(
        tangent * mapNormal.x + bitangent * mapNormal.y + geometricNormal * mapNormal.z);
}

float relativePointLightLambert(
    vec3 surfaceNormal,
    vec2 lightOffset,
    float lightRadius)
{
    // Model the light one radius above the sprite plane, then divide by the
    // geometric +Z Lambert term. The normalization cancels analytically, giving
    // an exact relative factor of 1 for a flat normal at every fragment.
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

float shadowSegmentVisibility(
    vec2 fragmentPosition,
    vec2 lightPosition,
    float sourceRadius,
    vec4 segment)
{
    if (sourceRadius <= 0.0)
    {
        return shadowSegmentBlocksLight(fragmentPosition, lightPosition, segment) ? 0.0 : 1.0;
    }

    float coordinateMagnitude = max(
        max(max(abs(fragmentPosition.x), abs(fragmentPosition.y)),
            max(abs(lightPosition.x), abs(lightPosition.y))),
        max(max(abs(segment.x), abs(segment.y)),
            max(max(abs(segment.z), abs(segment.w)), sourceRadius)));
    float coordinateScale = max(1.0, coordinateMagnitude * 0.25);
    float inverseCoordinateScale = 1.0 / coordinateScale;
    vec2 scaledFragmentPosition = fragmentPosition * inverseCoordinateScale;
    vec2 scaledLightPosition = lightPosition * inverseCoordinateScale;
    vec4 scaledSegment = segment * inverseCoordinateScale;
    float scaledSourceRadius = sourceRadius * inverseCoordinateScale;
    if (scaledSourceRadius <= 0.0)
    {
        return shadowSegmentBlocksLight(fragmentPosition, lightPosition, segment) ? 0.0 : 1.0;
    }

    vec2 lightRay = scaledLightPosition - scaledFragmentPosition;
    float lightDistance = length(lightRay);
    if (lightDistance <= scaledSourceRadius + 0.001 * inverseCoordinateScale)
    {
        return 1.0;
    }

    vec2 lightAxis = lightRay / lightDistance;
    vec2 startRelative = scaledSegment.xy - scaledFragmentPosition;
    vec2 endRelative = scaledSegment.zw - scaledFragmentPosition;
    float startAxial = dot(startRelative, lightAxis);
    float endAxial = dot(endRelative, lightAxis);
    float minimumAxial = 0.001 * lightDistance;
    float maximumAxial = 0.999 * lightDistance;
    if (minimumAxial <= 0.0 || maximumAxial <= minimumAxial)
    {
        return 1.0;
    }

    float axialDelta = endAxial - startAxial;
    float clippedStart = 0.0;
    float clippedEnd = 1.0;
    if (abs(axialDelta) < 0.00001 * lightDistance)
    {
        if (startAxial <= minimumAxial || startAxial >= maximumAxial)
        {
            return 1.0;
        }
    }
    else
    {
        float atMinimumAxial = (minimumAxial - startAxial) / axialDelta;
        float atMaximumAxial = (maximumAxial - startAxial) / axialDelta;
        clippedStart = max(clippedStart, min(atMinimumAxial, atMaximumAxial));
        clippedEnd = min(clippedEnd, max(atMinimumAxial, atMaximumAxial));
        if (clippedStart > clippedEnd)
        {
            return 1.0;
        }
    }

    vec2 segmentDirection = endRelative - startRelative;
    vec2 clippedStartPosition = startRelative + segmentDirection * clippedStart;
    vec2 clippedEndPosition = startRelative + segmentDirection * clippedEnd;
    float clippedStartAxial = clamp(
        dot(clippedStartPosition, lightAxis), minimumAxial, maximumAxial);
    float clippedEndAxial = clamp(
        dot(clippedEndPosition, lightAxis), minimumAxial, maximumAxial);
    float startProjection =
        cross2D(lightAxis, clippedStartPosition) * (lightDistance / clippedStartAxial);
    float endProjection =
        cross2D(lightAxis, clippedEndPosition) * (lightDistance / clippedEndAxial);
    float blockedMinimum = min(startProjection, endProjection);
    float blockedMaximum = max(startProjection, endProjection);
    float overlap = max(
        min(blockedMaximum, scaledSourceRadius) - max(blockedMinimum, -scaledSourceRadius),
        0.0);
    float blockedFraction = (overlap / scaledSourceRadius) * 0.5;
    return clamp(1.0 - blockedFraction, 0.0, 1.0);
}

void main()
{
    // Sample product/fixture texture and modulate by per-vertex color.
    // Premultiply alpha for the Sprite2D blend state (ONE, INV_SRC_ALPHA).
    vec4 tex = texture2D(s_tex, v_texcoord0);
    vec4 color = tex * v_color0;
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
                        shadowVisibility *= shadowSegmentVisibility(
                            v_worldPos,
                            pointLight.xy,
                            u_spriteLightColors[lightIndex].w,
                            u_spriteShadowSegments[segmentIndex]);
                        if (shadowVisibility <= 0.0)
                        {
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
                lighting += u_spriteLightColors[lightIndex].rgb * attenuation;
            }
            else
            {
                lighting += u_spriteLightColors[lightIndex].rgb * attenuation;
            }
        }
    }
    gl_FragColor = vec4(color.rgb * lighting * color.a, color.a);
}
