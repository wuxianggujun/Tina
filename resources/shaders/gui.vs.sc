$input a_position, a_color0
$output v_color0

#include <bgfx_shader.sh>

void main()
{
    v_color0 = a_color0;
    gl_Position = vec4(a_position.x, a_position.y, 0.0, 1.0);
} 