$input a_position, a_texcoord0, a_color0, i_data0, i_data1
$output v_color0, v_texcoord0

#include "common.sh"
#include <bgfx_shader.sh>

void main()
{
    // 从实例数据中获取变换信息
    vec2 position = i_data0.xy;
    vec2 scale = i_data0.zw;
    
    // 应用实例变换
    vec2 worldPos = position + (a_position.xy * scale);
    
    // 转换到裁剪空间
    vec4 pos = vec4(worldPos, 0.0, 1.0);
    gl_Position = mul(u_modelViewProj, pos);
    
    // 混合实例颜色和顶点颜色
    v_color0 = i_data1;
    v_texcoord0 = a_texcoord0;
}
