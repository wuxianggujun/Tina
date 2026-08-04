#include <bgfx_shader.sh>

void main()
{
	// The framebuffer has only a depth attachment; color output is discarded.
	gl_FragColor = vec4_splat(0.0);
}
