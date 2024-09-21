$input  v_texcoord0// in...

#include "./include/bgfx.sh"

SAMPLER2D(s_texColor,  0);

void main()
{
	vec4 color = texture2D(s_texColor, v_texcoord0) ;
	gl_FragColor = vec4(color.xyz, 1.0);
}
