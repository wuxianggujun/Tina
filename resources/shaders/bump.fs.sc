$input v_wpos, v_view, v_normal, v_tangent, v_bitangent, v_texcoord0// in...

/*
 * Copyright 2011-2024 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "./include/bgfx.sh"

SAMPLER2D(s_texColor,  0);
SAMPLER2D(s_texNormal, 1);

void main()
{
	vec4 color = texture2D(s_texColor, v_texcoord0) ;
	gl_FragColor = vec4(color.xyz, 1.0);
}
