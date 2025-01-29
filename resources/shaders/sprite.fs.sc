$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    // 直接使用顶点颜色，确保正确解析RGBA格式
    vec4 color = v_color0;
    gl_FragColor = vec4(color.bgr, color.a); // 交换BGR为RGB
} 