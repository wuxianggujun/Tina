$input v_texcoord0

#include "./include/bgfx.sh"

SAMPLER2D(s_tex, 0);

void main(){
     vec4 color = texture2D(s_tex, v_texcoord0);
     gl_FragColor = color;
}