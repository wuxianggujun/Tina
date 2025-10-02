$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

void main()
{
    // 基于 UV 的径向衰减：中心亮，边缘柔和透明
    vec2 p = v_texcoord0 - vec2(0.5, 0.5);
    float d = length(p) * 2.0;             // 约 1.0 为边缘
    float alpha = smoothstep(1.0, 0.0, d);  // 中心1，边缘0
    alpha = pow(alpha, 2.4);                // 更柔和的尘雾效果（非火花）
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * alpha);
}
