$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    vec4 color = v_color0;
    
    // 尝试采样纹理
    vec4 texColor = texture2D(s_texColor, v_texcoord0);
    
    // 如果纹理的alpha值为0（表示没有有效纹理），使用顶点颜色
    // 否则使用纹理颜色
    color = (texColor.a == 0.0) ? color : texColor;
    
    // 确保颜色通道顺序正确（BGRA -> RGBA）
    gl_FragColor = vec4(color.bgr, color.a);
} 