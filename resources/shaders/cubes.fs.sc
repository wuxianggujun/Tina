$input v_texcoord0

#include "./include/bgfx.sh"

SAMPLER2D(s_tex, 0);

void main(){
     vec4 color = texture2D(s_tex, v_texcoord0).rgba;
     gl_FragColor = color;
    //gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);  // 输出红色，检查是否正确显示
}