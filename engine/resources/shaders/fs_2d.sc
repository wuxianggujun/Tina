$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texture, 0);

void main()
{
    // vec4 color = texture2D(s_texture, v_texcoord0);
    // gl_FragColor = color * v_color0;
     // 强制输出红色，忽略纹理和顶点颜色
    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
