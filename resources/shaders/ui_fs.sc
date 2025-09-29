$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    // 采样单通道 R8 或 RGBA 纹理：取 R 通道作为覆盖度
    vec4 texCol = texture2D(s_texColor, v_texcoord0);
    // RmlUI 字体图集通常为 RGBA 预乘 alpha，直接相乘可得到期望着色
    gl_FragColor = v_color0 * texCol;
}
