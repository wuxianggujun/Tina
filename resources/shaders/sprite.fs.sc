$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    // 采样纹理
    vec4 texColor = texture2D(s_texColor, v_texcoord0);
    
    // 混合纹理颜色和顶点颜色
    gl_FragColor = texColor * v_color0;
} 