$input a_position
$output v_color0

#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_color0 = vec4(0.1, 0.7, 0.2, 1.0);
}
