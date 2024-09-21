$input a_position,a_texcoord0
$output v_texcoord0


#include "./include/bgfx.sh"

void main()
{
    vec4 worldPos = mul(u_modelViewProj, vec4(a_position, 1.0) );
	gl_Position = worldPos;
	v_texcoord0 = a_texcoord0;
}
