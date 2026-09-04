/*
 * Sprite2D custom-fragment contract. Every cooked Sprite2D fragment shader must include this
 * header, and must declare its own varying line as the first line of the file:
 *
 *   $input v_texcoord0, v_color0, v_worldPos
 *   #include <tina_sprite2d.sh>
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
 * The vertex stage stays engine-owned; only the fragment stage is replaceable. Write to
 * gl_FragColor with premultiplied alpha: the batch blend state is (ONE, INV_SRC_ALPHA).
 */
#ifndef TINA_SPRITE2D_SH_HEADER_GUARD
#define TINA_SPRITE2D_SH_HEADER_GUARD

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

#endif // TINA_SPRITE2D_SH_HEADER_GUARD
