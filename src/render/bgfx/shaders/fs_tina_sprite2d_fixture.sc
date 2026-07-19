$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

void main()
{
    vec3 gradient = vec3(v_texcoord0.x, v_texcoord0.y, 1.0 - (v_texcoord0.x * 0.5 + v_texcoord0.y * 0.5));
    vec3 straightRgb = gradient * v_color0.rgb;
    gl_FragColor = vec4(straightRgb * v_color0.a, v_color0.a);
}
