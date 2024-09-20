$input a_position,a_texcoord0
$output v_position,v_texcoord0

#include "./include/bgfx.sh"

void main(){
    v_position = a_position;
    gl_Position = vec4(a_position.xyz, 1.0);
    v_texcoord0 = a_texcoord0;
}