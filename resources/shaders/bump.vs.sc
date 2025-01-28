$input a_position
$output v_color0

#include <bgfx_shader.sh>

void main()
{
    v_color0 = vec4(1.0, 1.0, 1.0, 1.0); // 使用白色作为默认颜色
    gl_Position = mul(u_modelViewProj, vec4(a_position.x, a_position.y, 0.0, 1.0));
}
