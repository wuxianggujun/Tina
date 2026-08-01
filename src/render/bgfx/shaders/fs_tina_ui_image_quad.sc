$input v_color0, v_texcoord0, v_shapeParams

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    vec4 sample = texture2D(s_texColor, v_texcoord0);
    sample.rgb *= sample.a;
    gl_FragColor = sample * v_color0;
}
