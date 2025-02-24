$input v_texcoord0

#include "common.sh"
#include <bgfx_shader.sh>

// 纹理采样器
SAMPLER2D(s_texColor, 0);

void main()
{
    // 直接采样并输出纹理颜色
    gl_FragColor = texture2D(s_texColor, v_texcoord0);
}
