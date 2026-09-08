/*
 * Mesh3D custom-fragment contract. Every cooked Mesh3D fragment shader must include this header,
 * and must declare its own varying line as the first line of the file:
 *
 *   $input v_color0, v_texcoord0, v_normal, v_worldPos, v_tangent
 *   #include <tina_mesh3d.sh>
 *   vec4 tinaMesh3DFragment(vec4 baseColor, vec2 texcoord0, vec3 normal,
 *       vec3 worldPosition, vec4 tangent, float frontFaceSign) { return baseColor; }
 *
 * The varying line cannot live in here: shaderc scans `$input` off the raw file text before the
 * preprocessor runs (tools/shaderc/shaderc.cpp), so an included copy is never seen.
 *
 * Why the include is mandatory rather than convenient: bgfx dedupes uniforms by name, so a shader
 * that re-declares an engine uniform with a different type trips BX_ASSERT(uniform.m_type ==
 * _type) inside bgfx::createShader in Debug, and in Release silently widens the engine's uniform
 * and corrupts every draw that reads it. Declaring the whole engine set here turns that collision
 * into a shaderc redefinition error at cook time, which fails closed.
 *
 * The author returns linear lit RGBA, without emissive. The engine owns main():
 * material alpha masking, emissive and output encoding apply to every fragment.
 * Shadow alpha uses the same base texture and per-instance factor, not author output.
 */
#ifndef TINA_MESH3D_SH_HEADER_GUARD
#define TINA_MESH3D_SH_HEADER_GUARD

#include <bgfx_shader.sh>
#include <tina_alpha_mask.sh>

// glTF packing for s_texMR: G = roughness, B = metallic (R unused).
// s_texNormal: tangent-space RGB normal using the required vertex tangent TBN.
SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_texMR, 1);
SAMPLER2D(s_texNormal, 2);
SAMPLER2DSHADOW(s_csmAtlas, 3);
SAMPLERCUBE(s_iblDiffuse, 4);
SAMPLERCUBE(s_iblSpecular, 5);
SAMPLER2D(s_iblBrdf, 6);
SAMPLER2DSHADOW(s_spotShadowMap, 7);
SAMPLER2DSHADOW(s_pointShadowPosX, 8);
SAMPLER2DSHADOW(s_pointShadowNegX, 9);
SAMPLER2DSHADOW(s_pointShadowPosY, 10);
SAMPLER2DSHADOW(s_pointShadowNegY, 11);
SAMPLER2DSHADOW(s_pointShadowPosZ, 12);
SAMPLER2DSHADOW(s_pointShadowNegZ, 13);
SAMPLER2D(s_texEmissive, 14);

// xyz = world-space direction toward light; w = 1 when the slot is active.
uniform vec4 u_lightDirs[4];
// rgb = light color * intensity; w unused.
uniform vec4 u_lightColors[4];
// xyz = world-space position, w = positive influence radius; zero radius disables the slot.
uniform vec4 u_pointLightPosRadius[8];
// rgb = point-light color * intensity; w unused.
uniform vec4 u_pointLightColors[8];
// xyz = world-space position, w = positive influence radius; zero radius disables the slot.
uniform vec4 u_spotLightPosRadius[8];
// xyz = normalized world-space direction from light, w = inner cone cosine.
uniform vec4 u_spotLightDirInner[8];
// rgb = spot-light color * intensity, w = outer cone cosine.
uniform vec4 u_spotLightColorOuter[8];
// x = metallic factor, y = roughness factor, z = ambient scale, w = 1 if MR map bound.
uniform vec4 u_mrParams;
// x = 1 if normal map bound, yzw unused.
uniform vec4 u_normalParams;
// rgb = linear radiance the material emits on its own, w unused (ADR 0043).
uniform vec4 u_emissiveFactor;
uniform mat4 u_csmMatrices[4];
// Positive view-space far depth for cascades 0..3.
uniform vec4 u_csmSplitDepths;
// x = receiver depth bias, y = receiver normal bias, z = atlas texel size,
// w = shadowed directional-light slot + 1 (zero disables sampling).
uniform vec4 u_csmParams;
uniform mat4 u_spotShadowMatrix;
// x = receiver depth bias, y = receiver normal bias, z = map texel size,
// w = shadowed spot-light slot + 1 (zero disables sampling).
uniform vec4 u_spotShadowParams;
uniform mat4 u_pointShadowMatrices[6];
// x = receiver depth bias, y = receiver normal bias, z = map texel size,
// w = shadowed point-light slot + 1 (zero disables sampling).
uniform vec4 u_pointShadowParams;
// x = IBL intensity, y = maximum authored specular mip, z = 1 when enabled,
// w = environment rotation around world +Y in radians.
uniform vec4 u_iblParams;

vec3 tinaLinearToSrgb(vec3 color)
{
    vec3 positive = max(color, vec3_splat(0.0));
    vec3 low = positive * 12.92;
    vec3 high = 1.055 * pow(positive, vec3_splat(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3_splat(0.0031308), positive));
}

vec4 tinaMesh3DFragment(vec4 baseColor, vec2 texcoord0, vec3 normal,
    vec3 worldPosition, vec4 tangent, float frontFaceSign);

void main()
{
    // sRGB resources are decoded by hardware; never decode a sampled RGB twice.
    vec4 baseColor = texture2D(s_texColor, v_texcoord0) * v_color0;
    tinaApplyAlphaMask(baseColor.a);
    // shaderc lowers fragment inputs to main parameters on HLSL/SPIR-V. Pass
    // them explicitly so the author hook never depends on backend-local names.
    float frontFaceSign = gl_FrontFacing ? 1.0 : -1.0;
    vec4 shaded = tinaMesh3DFragment(baseColor, v_texcoord0, v_normal,
        v_worldPos, v_tangent, frontFaceSign);
    shaded.rgb += texture2D(s_texEmissive, v_texcoord0).rgb * u_emissiveFactor.rgb;
    // Offscreen targets retain linear radiance; only direct presentation encodes
    // here. HDR chains encode once in their final output transform.
    vec3 outputColor = u_alphaParams.w > 0.5 ? shaded.rgb : tinaLinearToSrgb(shaded.rgb);
    gl_FragColor = vec4(outputColor, u_alphaParams.z > 0.5 ? shaded.a : 1.0);
}

#endif // TINA_MESH3D_SH_HEADER_GUARD
