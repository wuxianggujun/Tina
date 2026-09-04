$input v_texcoord0, v_color0, v_worldPos

// The varying line above must be the first line of the file: shaderc scans $input off the raw text
// before the preprocessor runs, so it cannot come from the include below.
//
// tina_sprite2d.sh declares the engine-owned samplers and uniform set. Including it is mandatory:
// bgfx dedupes uniforms by name, so re-declaring one of those with a different type would corrupt
// every engine draw that reads it. With the header in, shaderc reports a redefinition instead.
#include <tina_sprite2d.sh>

// One program, many materials. Every sprite in this sample draws this same cooked binary; what
// differs per sprite is only the value set published under its own uniform binding key.
//   u_tint.rgb   = multiplier on the sampled colour
//   u_tint.a     = channel-swizzle selector, quantised: <0.5 keeps RGB, >=0.5 swaps R and B
//   u_uvAdjust.xy = UV offset in texel-independent units
//   u_uvAdjust.z  = UV scale about the quad centre
//   u_uvAdjust.w  = unused
uniform vec4 u_tint;
uniform vec4 u_uvAdjust;

// The author's own sampler, on top of the engine's s_tex. Register 2 is not a free choice: the
// Sprite2D engine set occupies stages 0 and 1, so 2 is the first stage an author may use and the
// cooker rejects any other number. A material that publishes no texture for this sampler gets the
// engine's 1x1 white fallback, which multiplies to a no-op -- that is what keeps the two materials
// that do not use a mask looking exactly as they did before this sampler existed.
SAMPLER2D(s_mask, 2);

void main()
{
    // UV transform about the centre rather than the origin, so a scale keeps the sprite's own
    // texture centred instead of drifting toward a corner.
    vec2 centred = v_texcoord0 - vec2(0.5, 0.5);
    vec2 uv = centred * u_uvAdjust.z + vec2(0.5, 0.5) + u_uvAdjust.xy;

    vec4 base = texture2D(s_tex, uv) * v_color0;

    // Sampled at the untransformed UV so the mask is independent of the material's zoom: the mask
    // used here is a single flat colour, and reading it through the zoomed UV would make a wrong
    // stage binding indistinguishable from a wrong UV.
    vec3 mask = texture2D(s_mask, v_texcoord0).rgb;
    vec3 tinted = base.rgb * u_tint.rgb * mask;

    // Swizzle proves the material read the alpha channel of its own value set: two sprites sharing
    // this program but not this number come out with different hues from identical texels.
    vec3 swizzled = mix(tinted, tinted.bgr, step(0.5, u_tint.a));

    // Premultiplied alpha: the Sprite2D batch blend state is (ONE, INV_SRC_ALPHA).
    gl_FragColor = vec4(swizzled * base.a, base.a);
}
