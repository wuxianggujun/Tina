$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);

void main()
{
    // Sample product/fixture texture and modulate by per-vertex color.
    // Premultiply alpha for the Sprite2D blend state (ONE, INV_SRC_ALPHA).
    vec4 tex = texture2D(s_tex, v_texcoord0);
    vec4 color = tex * v_color0;
    gl_FragColor = vec4(color.rgb * color.a, color.a);
}
