#include <bgfx_shader.sh>

$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0

void main()
{
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;
    gl_Position = mul(u_modelViewProj, vec4(a_position.x, a_position.y, 0.0, 1.0));
} 
