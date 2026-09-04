$input v_texcoord0, v_color0, v_worldPos

// The varying line above must be the first line of the file: shaderc scans $input off the raw
// text before the preprocessor runs, so it cannot come from the include below.
//
// tina_sprite2d.sh declares the engine-owned sampler and uniform set. Including it is mandatory:
// bgfx dedupes uniforms by name, so re-declaring one of those with a different type would corrupt
// every engine draw that reads it. With the header in, shaderc reports a redefinition instead.
#include <tina_sprite2d.sh>

// Author-declared uniform. Names are how the engine matches a value to a uniform, because the
// cooked binary's uniform order is a shaderc detail. vec4 is the only shape available.
//   x = seconds since state enter
//   y = pulse frequency in Hz
//   z = ring width in meters
//   w = unused
uniform vec4 u_pulse;

void main()
{
    vec4 base = texture2D(s_tex, v_texcoord0) * v_color0;

    // Expanding ring in world space, so the effect is independent of sprite size and rotation.
    float seconds = u_pulse.x;
    float frequency = max(u_pulse.y, 0.0001);
    float ringWidth = max(u_pulse.z, 0.0001);
    float phase = fract(seconds * frequency);
    float radius = length(v_worldPos);
    float ring = 1.0 - clamp(abs(radius - phase * 6.0) / ringWidth, 0.0, 1.0);

    // Breathing tint plus the ring highlight. Both are multiplicative on the sampled colour, so a
    // fully transparent texel stays transparent.
    float breathe = 0.65 + 0.35 * sin(seconds * frequency * 6.2831853);
    vec3 tinted = base.rgb * breathe + vec3(0.35, 0.65, 1.0) * ring * base.a;

    // Premultiplied alpha: the Sprite2D batch blend state is (ONE, INV_SRC_ALPHA).
    gl_FragColor = vec4(tinted * base.a, base.a);
}
