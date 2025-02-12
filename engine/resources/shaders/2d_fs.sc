$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
uniform vec4 u_useTexture;  // 使用 x 分量作为标志，1.0表示使用纹理，0.0表示不使用

void main()
{
    if (u_useTexture.x > 0.5) {
        // 有纹理时，混合纹理颜色和顶点颜色
        vec4 texColor = texture2D(s_texColor, v_texcoord0);
        gl_FragColor = texColor * v_color0;
    } else {
        // 无纹理时，直接使用顶点颜色
        gl_FragColor = v_color0;
    }
}
