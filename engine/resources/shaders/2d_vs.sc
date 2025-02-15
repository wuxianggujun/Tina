$input a_position, a_texcoord0, a_color0, i_data0, i_data1, i_data2, i_data3
$output v_color0, v_texcoord0

#include "common.sh"
#include <bgfx_shader.sh>

void main()
{
    // 从实例数据中获取变换信息
    vec2 position = i_data0.xy;  // 位置
    vec2 scale = i_data0.zw;     // 缩放
    
    // 计算世界空间位置：
    // 1. 将顶点从[-0.5, 0.5]范围缩放到实际大小
    // 2. 添加位置偏移
    vec2 worldPos = (a_position.xy * scale) + position;
    
    // 转换到裁剪空间
    vec4 pos = vec4(worldPos, 0.0, 1.0);
    gl_Position = mul(u_modelViewProj, pos);
    
    // 传递颜色
    v_color0 = i_data1;
    
    // 获取纹理数据
    vec4 texData = i_data2;      // UV矩形 (x,y,w,h)
    float texIndex = i_data3.x;  // 纹理索引
    
    // 计算纹理坐标
    if (texIndex >= 0.0) {
        // 对于有纹理的quad:
        // 1. 将输入的UV坐标(0-1范围)映射到指定的纹理矩形
        v_texcoord0 = texData.xy + (a_texcoord0 * texData.zw);
        
        // 将纹理索引存储在z坐标中用于调试
        gl_Position.z = 0.1;  // 纹理在后面
    } else {
        // 对于无纹理的quad，保持原始UV坐标
        v_texcoord0 = a_texcoord0;
        gl_Position.z = 0.0;  // 纯色矩形在前面
    }
}
