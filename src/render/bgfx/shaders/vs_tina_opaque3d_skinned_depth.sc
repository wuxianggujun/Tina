$input a_position, a_texcoord0, a_indices, a_weight
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>
#include <tina_skin_palette.sh>

void main()
{
    mat4 combined = mul(u_model[0], tinaSkinMatrix(ivec4(a_indices), a_weight));
    gl_Position = mul(u_viewProj, mul(combined, vec4(a_position, 1.0)));
    v_color0 = u_tinaSkinColor;
    v_texcoord0 = a_texcoord0;
}
