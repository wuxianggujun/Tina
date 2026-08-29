$input v_color0, v_texcoord0, v_shapeParams

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    // Not named `sample`: that is a reserved word in GLSL ES 3.0 (a multisample
    // interpolation qualifier), so the ESSL variant of this shader fails to compile
    // with "illegal use of reserved word". Desktop GLSL 120, SPIR-V and HLSL all
    // accepted it, which is why it survived until the OpenGLES profile was cooked.
    vec4 texel = texture2D(s_texColor, v_texcoord0);
    texel.rgb *= texel.a;
    gl_FragColor = texel * v_color0;
}
