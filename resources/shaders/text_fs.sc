$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_text, 0);

void main()
{
// 调试：输出字形覆盖度到 RGB，若能看到灰度字形，说明纹理更新/采样正常
vec4 sampleCol = texture2D(s_text, v_texcoord0);
gl_FragColor = vec4(sampleCol.rrr, 1.0);
}
