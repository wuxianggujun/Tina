$input a_position, a_texcoord0, a_color0
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>

void main()
{
    // 转换顶点位置
    vec4 pos = vec4(a_position.xy, 0.0, 1.0);
    gl_Position = mul(u_modelViewProj, pos);
    
    // 传递颜色和纹理坐标到片段着色器
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
}
