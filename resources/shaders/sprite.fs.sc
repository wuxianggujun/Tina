$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    vec4 color = texture2D(s_texColor, v_texcoord0);
    // gl_FragColor = color * v_color0;
    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); // 输出纯红色
} 