$input v_color0, v_texcoord0, v_normal, v_worldPos

#include <bgfx_shader.sh>

// Experimental Opaque3D metallic-roughness hybrid (RENDER-001 first slice).
// Honesty: single fixed directional light + constant ambient; no IBL, shadows,
// multi-light, or cooked MR factors yet. Defaults when maps unbound:
//   baseColor = white * instance factor; metallic=0; roughness=1.
// glTF packing for s_texMR: G = roughness, B = metallic (R unused).

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_texMR, 1);

// xyz = world-space direction toward the light (normalized preferred), w unused.
uniform vec4 u_lightDir;
// rgb = light color * intensity, w unused.
uniform vec4 u_lightColor;
// x = metallic factor, y = roughness factor, z = ambient scale, w = 1 if MR map bound.
uniform vec4 u_mrParams;

void main()
{
	vec4 texel = texture2D(s_texColor, v_texcoord0);
	vec4 baseColor = v_color0 * texel;

	float metallic = clamp(u_mrParams.x, 0.0, 1.0);
	float roughness = clamp(u_mrParams.y, 0.04, 1.0);
	if (u_mrParams.w > 0.5)
	{
		vec4 mrSample = texture2D(s_texMR, v_texcoord0);
		// glTF metallic-roughness texture: G roughness, B metallic.
		roughness = clamp(mrSample.g * u_mrParams.y, 0.04, 1.0);
		metallic = clamp(mrSample.b * u_mrParams.x, 0.0, 1.0);
	}

	vec3 N = normalize(v_normal);
	vec3 L = normalize(u_lightDir.xyz);
	// Camera world position from inverse view (bgfx built-in).
	vec3 eyePos = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 V = normalize(eyePos - v_worldPos);
	vec3 H = normalize(L + V);

	float NdotL = max(dot(N, L), 0.0);
	float NdotH = max(dot(N, H), 0.0);

	vec3 albedo = baseColor.rgb;
	vec3 F0 = mix(vec3_splat(0.04), albedo, metallic);
	vec3 diffuse = albedo * (1.0 - metallic) * NdotL;

	// Shininess falls with roughness; metallic boosts specular lobe.
	float shininess = exp2(10.0 * (1.0 - roughness) + 1.0);
	float specular = pow(NdotH, shininess) * NdotL;
	vec3 specularColor = F0 * specular;

	float ambientScale = max(u_mrParams.z, 0.0);
	vec3 ambient = albedo * ambientScale * (1.0 - metallic * 0.6) + F0 * (ambientScale * 0.25);

	vec3 lit = ambient + u_lightColor.rgb * (diffuse + specularColor);
	gl_FragColor = vec4(lit, baseColor.a);
}
