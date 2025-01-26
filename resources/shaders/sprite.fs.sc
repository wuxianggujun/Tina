$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texture, 0);

void main() {
    vec4 texColor = texture2D(s_texture, v_texcoord0);
    gl_FragColor = texColor * v_color0;
} 