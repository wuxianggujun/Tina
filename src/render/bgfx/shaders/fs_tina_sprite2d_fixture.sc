$input v_texcoord0, v_color0, v_worldPos

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);

// xy = world position, z = radius in meters, w = active slot.
uniform vec4 u_spriteLightPosRadius[8];
// rgb = linear light color * intensity, w unused.
uniform vec4 u_spriteLightColors[8];
// x = ambient scale, y = active point-light count, zw unused.
uniform vec4 u_spriteLightParams;

void main()
{
    // Sample product/fixture texture and modulate by per-vertex color.
    // Premultiply alpha for the Sprite2D blend state (ONE, INV_SRC_ALPHA).
    vec4 tex = texture2D(s_tex, v_texcoord0);
    vec4 color = tex * v_color0;
    vec3 lighting = vec3_splat(max(u_spriteLightParams.x, 0.0));
    for (int lightIndex = 0; lightIndex < 8; ++lightIndex)
    {
        if (u_spriteLightPosRadius[lightIndex].w > 0.5)
        {
            vec3 pointLight = u_spriteLightPosRadius[lightIndex].xyz;
            float distanceToLight = length(pointLight.xy - v_worldPos);
            float attenuation = max(1.0 - distanceToLight / pointLight.z, 0.0);
            lighting += u_spriteLightColors[lightIndex].rgb * attenuation;
        }
    }
    gl_FragColor = vec4(color.rgb * lighting * color.a, color.a);
}
