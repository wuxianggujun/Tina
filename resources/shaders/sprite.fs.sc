$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    // v_color0 已经是正确的RGBA格式
    vec4 color = v_color0;
    
    // 尝试采样纹理
    vec4 texColor = texture2D(s_texColor, v_texcoord0);
    
    // 如果纹理的alpha值为0（表示没有有效纹理），使用顶点颜色
    // 否则使用纹理颜色
    color = (texColor.a == 0.0) ? color : texColor;
    
    // 直接输出颜色，因为Color类已经处理了颜色格式
    gl_FragColor = color;
} 