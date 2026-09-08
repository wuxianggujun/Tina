$input v_color0, v_texcoord0

#include <bgfx_shader.sh>
#include <tina_alpha_mask.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
	if (u_alphaParams.x > 0.5)
	{
		tinaApplyAlphaMask(texture2D(s_texColor, v_texcoord0).a * v_color0.a);
	}
	// The framebuffer has only a depth attachment; color output is discarded.
	gl_FragColor = vec4_splat(0.0);
}
