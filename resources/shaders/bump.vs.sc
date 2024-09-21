$input a_position, a_normal, a_tangent, a_texcoord0
$output v_wpos, v_view, v_normal, v_tangent, v_bitangent, v_texcoord0

/*
 * Copyright 2011-2024 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "./include/bgfx.sh"

void main()
{
	gl_Position = vec4(a_position.xyz, 1.0);
	v_texcoord0 = a_texcoord0;
}
