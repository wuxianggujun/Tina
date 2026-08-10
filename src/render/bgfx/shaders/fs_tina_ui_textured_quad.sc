$input v_color0, v_texcoord0, v_shapeParams

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

float ellipseSignedDistance(vec2 localPoint, vec2 halfExtent)
{
    vec2 safeHalfExtent = max(halfExtent, vec2(0.0001, 0.0001));
    vec2 normalized = localPoint / safeHalfExtent;
    float normalizedLength = length(normalized);
    float gradientLength = length(
        localPoint / (safeHalfExtent * safeHalfExtent));
    float minimumExtent = min(safeHalfExtent.x, safeHalfExtent.y);
    float approximateDistance = -minimumExtent;
    if (gradientLength > 0.0001)
    {
        approximateDistance = normalizedLength *
                              (normalizedLength - 1.0) /
                              gradientLength;
    }
    return max(approximateDistance, -minimumExtent);
}

void main()
{
    // R8 atlas pages and the solid 1x1 white page both sample .r as coverage.
    float coverage = texture2D(s_texColor, v_texcoord0).r;
    float shapeParameter = v_shapeParams.z;
    if (shapeParameter < 0.0)
    {
        vec2 halfExtent = v_shapeParams.xy * 0.5;
        vec2 localPoint = (v_texcoord0 - vec2(0.5, 0.5)) * v_shapeParams.xy;
        float signedDistance = ellipseSignedDistance(localPoint, halfExtent);
        float outerCoverage = clamp(0.5 - signedDistance, 0.0, 1.0);
        float strokeWidth = max(-shapeParameter - 1.0, 0.0);
        if (strokeWidth > 0.0 &&
            strokeWidth < min(halfExtent.x, halfExtent.y))
        {
            // Offset the outer screen-space signed distance instead of shrinking
            // both ellipse axes. This keeps the inward band width uniform for
            // non-circular ellipses and anisotropic logical-to-pixel projection.
            float innerCoverage = clamp(
                0.5 - (signedDistance + strokeWidth), 0.0, 1.0);
            outerCoverage = clamp(outerCoverage - innerCoverage, 0.0, 1.0);
        }
        coverage *= outerCoverage;
    }
    else if (shapeParameter > 0.0)
    {
        float radius = shapeParameter;
        vec2 halfExtent = v_shapeParams.xy * 0.5;
        vec2 localPoint = (v_texcoord0 - vec2(0.5, 0.5)) * v_shapeParams.xy;
        vec2 outside = abs(localPoint) - (halfExtent - vec2(radius, radius));
        float distance = length(max(outside, vec2(0.0, 0.0)))
                       + min(max(outside.x, outside.y), 0.0) - radius;
        coverage *= clamp(0.5 - distance, 0.0, 1.0);
    }
    gl_FragColor = v_color0 * coverage;
}
