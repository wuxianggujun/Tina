$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_text, 0);

void main()
{
    vec4 sampleCol = texture2D(s_text, v_texcoord0);
    float cov = sampleCol.r; // 期望 R8 覆盖度图集
    vec4 outCol = vec4(v_color0.rgb, v_color0.a * cov);
    gl_FragColor = outCol;
}

