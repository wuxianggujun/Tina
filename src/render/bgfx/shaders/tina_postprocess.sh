#ifndef TINA_POSTPROCESS_SH
#define TINA_POSTPROCESS_SH
#include <bgfx_shader.sh>

// Custom fragments declare: $input v_texcoord0
// Inputs and outputs are scene-linear. The engine owns the final output transform.
// The bound source/auxiliary is one explicit mip, not a whole-texture implicit LOD.
SAMPLER2D(s_postSource, 0);
SAMPLER2D(s_postAuxiliary, 1);
// xy = reciprocal source extent; z = RT bottom-left origin; w = logical mip.
uniform vec4 u_postSourceInfo;
// xy = destination extent; z = logical destination mip; w reserved.
uniform vec4 u_postDestinationInfo;
// Engine-only effect controls, reserved in custom fragments to prevent collisions.
uniform vec4 u_postParams;
uniform vec4 u_postBloom;
uniform vec4 u_postFog;
uniform vec4 u_postFogColor;
uniform vec4 u_postCamera;
uniform vec4 u_postViewport;
uniform mat4 u_postInverseViewProjection;
uniform mat4 u_postDecalFromWorld;
uniform vec4 u_postDecalColor;

vec2 tinaPostSourceUv(vec2 uv)
{
    return vec2(uv.x, mix(uv.y, 1.0 - uv.y, u_postSourceInfo.z));
}
vec4 tinaPostSample(vec2 uv)
{
    return texture2D(s_postSource, tinaPostSourceUv(uv));
}
vec4 tinaPostSampleAuxiliary(vec2 uv)
{
    return texture2D(s_postAuxiliary, tinaPostSourceUv(uv));
}
#endif
