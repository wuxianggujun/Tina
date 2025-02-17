$input v_color0, v_texcoord0,v_useTexture

#include "common.sh"
#include <bgfx_shader.sh>

// 纹理采样器
SAMPLER2D(s_texColor, 0);

// 是否使用纹理的标志 (x: 是否使用纹理, y: 纹理索引)
uniform vec4 u_useTexture;

void main()
{
    vec4 color = v_color0;

    // 同时检查全局纹理标志和实例纹理标志
    if (u_useTexture.x > 0.0 && v_useTexture > 0.0) {
        // 采样纹理并应用颜色
        vec4 texColor = texture2D(s_texColor, v_texcoord0);
        
        // 预乘alpha
        texColor.rgb *= texColor.a;
        color *= texColor;
    }
    
    // 只有完全透明的片段才丢弃
    if (color.a <= 0.0) {
        discard;
    }
    
    gl_FragColor = color;
}
