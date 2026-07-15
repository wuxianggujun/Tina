$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_text, 0);

void main()
{
    vec4 texel = texture2D(s_text, v_texcoord0);
    float cov = texel.a;          // 覆盖度采样自 A 通道
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * cov);
}
