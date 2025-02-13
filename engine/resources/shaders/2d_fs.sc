$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

// 纹理采样器
SAMPLER2D(s_texColor, 0);

// 是否使用纹理的标志
uniform vec4 u_useTexture;

void main()
{
    vec4 color = v_color0;
    
    // 如果使用纹理,则采样纹理并与顶点颜色混合
    if (u_useTexture.x > 0.5) {
        vec4 texColor = texture2D(s_texColor, v_texcoord0);
        color *= texColor;
    }
    
    gl_FragColor = color;
}
