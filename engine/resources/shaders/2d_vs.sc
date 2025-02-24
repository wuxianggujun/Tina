$input a_position, a_texcoord0
$output v_texcoord0

#include "common.sh"
#include <bgfx_shader.sh>

void main()
{
    // 转换到裁剪空间
    vec4 pos = vec4(a_position.xy, 0.0, 1.0);
    gl_Position = mul(u_proj, mul(u_view, pos));
    
    // 传递纹理坐标
    v_texcoord0 = a_texcoord0;
}
