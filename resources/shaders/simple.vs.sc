 $input a_position, a_color0
 $output v_color0
 
#include "./include/bgfx.sh"
 
 void main()
 {
      gl_Position = vec4(a_position, 1.0);
 }