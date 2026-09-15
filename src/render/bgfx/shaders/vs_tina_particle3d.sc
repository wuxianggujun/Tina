$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>

void main()
{
    // u_viewProj, not u_modelViewProj: billboard corners arrive already expanded
    // into world space against the camera basis, so there is no per-particle model
    // matrix to apply and the backend never calls setTransform for this program.
    gl_Position = mul(u_viewProj, vec4(a_position, 1.0));
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
}
