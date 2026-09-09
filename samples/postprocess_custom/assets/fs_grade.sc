$input v_texcoord0
#include <tina_postprocess.sh>

uniform vec4 u_grade;

void main()
{
    vec4 source = tinaPostSample(v_texcoord0);
    vec3 gain = mix(vec3_splat(1.0), u_grade.rgb, u_grade.a);
    gl_FragColor = vec4(source.rgb * gain, source.a);
}
