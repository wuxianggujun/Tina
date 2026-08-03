$input v_color0, v_texcoord0, v_normal, v_worldPos, v_tangent

#include <bgfx_shader.sh>

// Experimental Opaque3D metallic-roughness hybrid (RENDER-001).
// Honesty: up to four directional + eight point + eight spot lights and constant ambient; no IBL/shadows.
// Cooked factors via u_mrParams; optional MR + normal maps.
// glTF packing for s_texMR: G = roughness, B = metallic (R unused).
// s_texNormal: tangent-space RGB normal. Vertex-tangent meshes use their TBN;
// existing P3N3UV2 meshes retain the derivative cotangent-frame fallback.

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_texMR, 1);
SAMPLER2D(s_texNormal, 2);

// xyz = world-space direction toward light; w = 1 when the slot is active.
uniform vec4 u_lightDirs[4];
// rgb = light color * intensity; w unused.
uniform vec4 u_lightColors[4];
// xyz = world-space position, w = positive influence radius; zero radius disables the slot.
uniform vec4 u_pointLightPosRadius[8];
// rgb = point-light color * intensity; w unused.
uniform vec4 u_pointLightColors[8];
// xyz = world-space position, w = positive influence radius; zero radius disables the slot.
uniform vec4 u_spotLightPosRadius[8];
// xyz = normalized world-space direction from light, w = inner cone cosine.
uniform vec4 u_spotLightDirInner[8];
// rgb = spot-light color * intensity, w = outer cone cosine.
uniform vec4 u_spotLightColorOuter[8];
// x = metallic factor, y = roughness factor, z = ambient scale, w = 1 if MR map bound.
uniform vec4 u_mrParams;
// x = 1 if normal map bound, y = 1 if the current mesh has vertex tangents, zw unused.
uniform vec4 u_normalParams;

vec3 safeNormalize(vec3 value)
{
	return value * inversesqrt(max(dot(value, value), 0.00000001));
}

vec3 shadeLight(vec3 N, vec3 V, vec3 L, vec3 lightRgb, vec3 albedo, vec3 F0, float metallic,
	float roughness)
{
	float NdotL = max(dot(N, L), 0.0);
	vec3 H = safeNormalize(L + V);
	float NdotH = max(dot(N, H), 0.0);
	vec3 diffuse = albedo * (1.0 - metallic) * NdotL;
	float shininess = exp2(10.0 * (1.0 - roughness) + 1.0);
	float specular = pow(NdotH, shininess) * NdotL;
	return lightRgb * (diffuse + F0 * specular);
}

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

	vec3 N = safeNormalize(v_normal);
	if (u_normalParams.x > 0.5)
	{
		vec3 T;
		vec3 B;
		if (u_normalParams.y > 0.5)
		{
			T = safeNormalize(v_tangent.xyz - N * dot(N, v_tangent.xyz));
			float tangentHandedness = v_tangent.w < 0.0 ? -1.0 : 1.0;
			B = cross(N, T) * tangentHandedness;
		}
		else
		{
			vec3 dp1 = dFdx(v_worldPos);
			vec3 dp2 = dFdy(v_worldPos);
			vec2 duv1 = dFdx(v_texcoord0);
			vec2 duv2 = dFdy(v_texcoord0);
			vec3 dp2perp = cross(dp2, N);
			vec3 dp1perp = cross(N, dp1);
			T = dp2perp * duv1.x + dp1perp * duv2.x;
			B = dp2perp * duv1.y + dp1perp * duv2.y;
			float invMax = inversesqrt(max(max(dot(T, T), dot(B, B)), 0.00000001));
			T *= invMax;
			B *= invMax;
		}
		vec3 mapN = texture2D(s_texNormal, v_texcoord0).xyz * 2.0 - 1.0;
		// glTF: green channel often OpenGL-style; keep as authored.
		vec3 mappedN = T * mapN.x + B * mapN.y + N * mapN.z;
		N = safeNormalize(mappedN);
	}

	// Camera world position from inverse view (bgfx built-in).
	vec3 eyePos = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 V = safeNormalize(eyePos - v_worldPos);

	vec3 albedo = baseColor.rgb;
	vec3 F0 = mix(vec3_splat(0.04), albedo, metallic);

	vec3 lit = vec3_splat(0.0);
	for (int lightIndex = 0; lightIndex < 4; ++lightIndex)
	{
		if (u_lightDirs[lightIndex].w > 0.5)
		{
			lit += shadeLight(N, V, safeNormalize(u_lightDirs[lightIndex].xyz),
				u_lightColors[lightIndex].rgb, albedo, F0, metallic, roughness);
		}
	}
	for (int pointLightIndex = 0; pointLightIndex < 8; ++pointLightIndex)
	{
		float influenceRadius = u_pointLightPosRadius[pointLightIndex].w;
		if (influenceRadius > 0.0)
		{
			vec3 lightOffset = u_pointLightPosRadius[pointLightIndex].xyz - v_worldPos;
			float lightDistance = length(lightOffset);
			float attenuation = max(1.0 - lightDistance / influenceRadius, 0.0);
			if (attenuation > 0.0)
			{
				lit += shadeLight(N, V, safeNormalize(lightOffset),
					u_pointLightColors[pointLightIndex].rgb * attenuation,
					albedo, F0, metallic, roughness);
			}
		}
	}
	for (int spotLightIndex = 0; spotLightIndex < 8; ++spotLightIndex)
	{
		float influenceRadius = u_spotLightPosRadius[spotLightIndex].w;
		if (influenceRadius > 0.0)
		{
			vec3 fragmentFromLight = v_worldPos - u_spotLightPosRadius[spotLightIndex].xyz;
			float lightDistance = length(fragmentFromLight);
			float radialAttenuation = max(1.0 - lightDistance / influenceRadius, 0.0);
			float coneCosine = dot(safeNormalize(fragmentFromLight),
				u_spotLightDirInner[spotLightIndex].xyz);
			float angularAttenuation = smoothstep(u_spotLightColorOuter[spotLightIndex].w,
				u_spotLightDirInner[spotLightIndex].w, coneCosine);
			float attenuation = radialAttenuation * angularAttenuation;
			if (attenuation > 0.0)
			{
				lit += shadeLight(N, V, safeNormalize(-fragmentFromLight),
					u_spotLightColorOuter[spotLightIndex].rgb * attenuation,
					albedo, F0, metallic, roughness);
			}
		}
	}

	float ambientScale = max(u_mrParams.z, 0.0);
	vec3 ambient = albedo * ambientScale * (1.0 - metallic * 0.6) + F0 * (ambientScale * 0.25);
	lit += ambient;

	gl_FragColor = vec4(lit, baseColor.a);
}
