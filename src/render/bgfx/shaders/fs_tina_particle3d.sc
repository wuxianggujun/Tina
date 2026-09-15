$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

// Stage 0 is baked into the cooked binary as a register index, so the backend must
// bind the particle texture at stage 0 and nowhere else.
SAMPLER2D(s_texColor, 0);

void main()
{
    // Premultiplied output lets one program serve both particle blend states:
    // AlphaBlend is (ONE, INV_SRC_ALPHA) and Additive is (ONE, ONE). Straight alpha
    // would make the additive state ignore alpha entirely, so a fading additive
    // particle would stay at full intensity until it vanished.
    vec4 texel = texture2D(s_texColor, v_texcoord0);
    vec4 color = texel * v_color0;
    gl_FragColor = vec4(color.rgb * color.a, color.a);
}
