$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    // R8 atlas pages and the solid 1x1 white page both sample .r as coverage.
    float coverage = texture2D(s_texColor, v_texcoord0).r;
    gl_FragColor = v_color0 * coverage;
}
