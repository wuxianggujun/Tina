$input v_color0, v_texcoord0, v_normal, v_worldPos, v_tangent

#include <bgfx_shader.sh>

// Opaque3D metallic-roughness PBR (RENDER-001-IBL).
// Up to four directional + eight point + eight spot lights, one four-cascade
// directional shadow atlas, one spot-light shadow map, six point-light shadow
// maps, and one prefiltered image-based lighting environment.
// Cooked factors via u_mrParams; optional MR + normal maps.
// glTF packing for s_texMR: G = roughness, B = metallic (R unused).
// s_texNormal: tangent-space RGB normal using the required vertex tangent TBN.

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_texMR, 1);
SAMPLER2D(s_texNormal, 2);
SAMPLER2DSHADOW(s_csmAtlas, 3);
SAMPLERCUBE(s_iblDiffuse, 4);
SAMPLERCUBE(s_iblSpecular, 5);
SAMPLER2D(s_iblBrdf, 6);
SAMPLER2DSHADOW(s_spotShadowMap, 7);
SAMPLER2DSHADOW(s_pointShadowPosX, 8);
SAMPLER2DSHADOW(s_pointShadowNegX, 9);
SAMPLER2DSHADOW(s_pointShadowPosY, 10);
SAMPLER2DSHADOW(s_pointShadowNegY, 11);
SAMPLER2DSHADOW(s_pointShadowPosZ, 12);
SAMPLER2DSHADOW(s_pointShadowNegZ, 13);

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
// x = 1 if normal map bound, yzw unused.
uniform vec4 u_normalParams;
uniform mat4 u_csmMatrices[4];
// Positive view-space far depth for cascades 0..3.
uniform vec4 u_csmSplitDepths;
// x = receiver depth bias, y = receiver normal bias, z = atlas texel size,
// w = shadowed directional-light slot + 1 (zero disables sampling).
uniform vec4 u_csmParams;
uniform mat4 u_spotShadowMatrix;
// x = receiver depth bias, y = receiver normal bias, z = map texel size,
// w = shadowed spot-light slot + 1 (zero disables sampling).
uniform vec4 u_spotShadowParams;
uniform mat4 u_pointShadowMatrices[6];
// x = receiver depth bias, y = receiver normal bias, z = map texel size,
// w = shadowed point-light slot + 1 (zero disables sampling).
uniform vec4 u_pointShadowParams;
// x = IBL intensity, y = maximum authored specular mip, z = 1 when enabled,
// w = environment rotation around world +Y in radians.
uniform vec4 u_iblParams;

#define TINA_PI 3.14159265359

vec3 safeNormalize(vec3 value)
{
	return value * inversesqrt(max(dot(value, value), 0.00000001));
}

vec3 srgbToLinear(vec3 color)
{
	vec3 nonNegative = max(color, vec3_splat(0.0));
	vec3 low = nonNegative / 12.92;
	vec3 high = pow((nonNegative + 0.055) / 1.055, vec3_splat(2.4));
	return mix(low, high, step(vec3_splat(0.04045), nonNegative));
}

vec3 linearToSrgb(vec3 color)
{
	vec3 nonNegative = max(color, vec3_splat(0.0));
	vec3 low = nonNegative * 12.92;
	vec3 high = 1.055 * pow(nonNegative, vec3_splat(1.0 / 2.4)) - 0.055;
	return mix(low, high, step(vec3_splat(0.0031308), nonNegative));
}

float distributionGgx(vec3 N, vec3 H, float roughness)
{
	float alpha = roughness * roughness;
	float alphaSquared = alpha * alpha;
	float NdotH = max(dot(N, H), 0.0);
	float denominator = NdotH * NdotH * (alphaSquared - 1.0) + 1.0;
	return alphaSquared / max(TINA_PI * denominator * denominator, 0.0000001);
}

float geometrySchlickGgx(float NdotDirection, float roughness)
{
	float r = roughness + 1.0;
	float k = r * r / 8.0;
	return NdotDirection / max(NdotDirection * (1.0 - k) + k, 0.0000001);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
	return geometrySchlickGgx(max(dot(N, V), 0.0), roughness)
		* geometrySchlickGgx(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosine, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosine, vec3 F0, float roughness)
{
	return F0 + (max(vec3_splat(1.0 - roughness), F0) - F0)
		* pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

vec3 shadeLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, vec3 F0, float metallic,
	float roughness)
{
	vec3 H = safeNormalize(V + L);
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float HdotV = max(dot(H, V), 0.0);
	float distribution = distributionGgx(N, H, roughness);
	float geometry = geometrySmith(N, V, L, roughness);
	vec3 fresnel = fresnelSchlick(HdotV, F0);
	vec3 specular = distribution * geometry * fresnel
		/ max(4.0 * NdotV * NdotL, 0.0000001);
	vec3 diffuseWeight = (1.0 - fresnel) * (1.0 - metallic);
	return (diffuseWeight * albedo / TINA_PI + specular) * radiance * NdotL;
}

vec3 rotateEnvironmentDirection(vec3 direction)
{
	float angle = u_iblParams.w;
	float sine = sin(angle);
	float cosine = cos(angle);
	return vec3(cosine * direction.x - sine * direction.z,
		direction.y,
		sine * direction.x + cosine * direction.z);
}

vec3 shadeImageBasedLighting(vec3 N, vec3 V, vec3 albedo, vec3 F0, float metallic,
	float roughness)
{
	float NdotV = max(dot(N, V), 0.0);
	vec3 fresnel = fresnelSchlickRoughness(NdotV, F0, roughness);
	vec3 diffuseWeight = (1.0 - fresnel) * (1.0 - metallic);
	vec3 irradiance = textureCube(s_iblDiffuse, rotateEnvironmentDirection(N)).rgb;
	vec3 diffuse = irradiance * albedo;
	vec3 reflection = reflect(-V, N);
	vec3 prefiltered = textureCubeLod(s_iblSpecular,
		rotateEnvironmentDirection(reflection), roughness * max(u_iblParams.y, 0.0)).rgb;
	vec2 brdf = texture2D(s_iblBrdf, vec2(NdotV, roughness)).rg;
	return (diffuseWeight * diffuse + prefiltered * (fresnel * brdf.x + brdf.y))
		* max(u_iblParams.x, 0.0);
}

float sampleCascadedDirectionalShadow(vec3 worldPosition, vec3 worldNormal)
{
	if (u_csmParams.w < 0.5)
	{
		return 1.0;
	}

	float viewDepth = -mul(u_view, vec4(worldPosition, 1.0)).z;
	if (viewDepth <= 0.0 || viewDepth > u_csmSplitDepths.w)
	{
		return 1.0;
	}

	int cascadeIndex = 0;
	if (viewDepth > u_csmSplitDepths.x)
	{
		cascadeIndex = 1;
	}
	if (viewDepth > u_csmSplitDepths.y)
	{
		cascadeIndex = 2;
	}
	if (viewDepth > u_csmSplitDepths.z)
	{
		cascadeIndex = 3;
	}

	vec3 biasedWorldPosition = worldPosition + worldNormal * max(u_csmParams.y, 0.0);
	vec4 biasedWorld = vec4(biasedWorldPosition, 1.0);
	vec4 shadowVarying = mul(u_csmMatrices[0], biasedWorld);
	if (cascadeIndex == 1)
	{
		shadowVarying = mul(u_csmMatrices[1], biasedWorld);
	}
	else if (cascadeIndex == 2)
	{
		shadowVarying = mul(u_csmMatrices[2], biasedWorld);
	}
	else if (cascadeIndex == 3)
	{
		shadowVarying = mul(u_csmMatrices[3], biasedWorld);
	}
	if (shadowVarying.w <= 0.0)
	{
		return 1.0;
	}
	vec3 shadowCoord = shadowVarying.xyz / shadowVarying.w;
	vec2 tileMinimum = vec2(
		(cascadeIndex == 1 || cascadeIndex == 3) ? 0.5 : 0.0,
		cascadeIndex >= 2 ? 0.5 : 0.0);
	vec2 tileMaximum = tileMinimum + vec2_splat(0.5);
	bool outside = any(lessThan(shadowCoord.xy, tileMinimum))
		|| any(greaterThan(shadowCoord.xy, tileMaximum))
		|| shadowCoord.z < 0.0 || shadowCoord.z > 1.0;
	if (outside)
	{
		return 1.0;
	}

	float compareDepth = shadowCoord.z - max(u_csmParams.x, 0.0);
	vec2 texel = vec2_splat(u_csmParams.z);
	vec2 sampleMinimum = tileMinimum + texel * 0.5;
	vec2 sampleMaximum = tileMaximum - texel * 0.5;
	float visibility = 0.0;
	for (int sampleY = -1; sampleY <= 1; ++sampleY)
	{
		for (int sampleX = -1; sampleX <= 1; ++sampleX)
		{
			vec2 sampleUv = clamp(shadowCoord.xy + texel * vec2(float(sampleX), float(sampleY)),
				sampleMinimum, sampleMaximum);
			visibility += shadow2D(s_csmAtlas, vec3(sampleUv, compareDepth));
		}
	}
	return visibility / 9.0;
}

float sampleSpotLightShadow(vec3 worldPosition, vec3 worldNormal)
{
	if (u_spotShadowParams.w < 0.5)
	{
		return 1.0;
	}

	vec3 biasedWorldPosition = worldPosition
		+ worldNormal * max(u_spotShadowParams.y, 0.0);
	vec4 shadowVarying = mul(u_spotShadowMatrix, vec4(biasedWorldPosition, 1.0));
	if (shadowVarying.w <= 0.0)
	{
		return 1.0;
	}
	vec3 shadowCoord = shadowVarying.xyz / shadowVarying.w;
	bool outside = any(lessThan(shadowCoord.xy, vec2_splat(0.0)))
		|| any(greaterThan(shadowCoord.xy, vec2_splat(1.0)))
		|| shadowCoord.z < 0.0 || shadowCoord.z > 1.0;
	if (outside)
	{
		return 1.0;
	}

	float compareDepth = shadowCoord.z - max(u_spotShadowParams.x, 0.0);
	vec2 texel = vec2_splat(u_spotShadowParams.z);
	vec2 sampleMinimum = texel * 0.5;
	vec2 sampleMaximum = vec2_splat(1.0) - sampleMinimum;
	float visibility = 0.0;
	for (int sampleY = -1; sampleY <= 1; ++sampleY)
	{
		for (int sampleX = -1; sampleX <= 1; ++sampleX)
		{
			vec2 sampleUv = clamp(
				shadowCoord.xy + texel * vec2(float(sampleX), float(sampleY)),
				sampleMinimum, sampleMaximum);
			visibility += shadow2D(s_spotShadowMap, vec3(sampleUv, compareDepth));
		}
	}
	return visibility / 9.0;
}

vec4 pointLightShadowVarying(int faceIndex, vec4 worldPosition)
{
	vec4 shadowPosition = mul(u_pointShadowMatrices[0], worldPosition);
	if (faceIndex == 1)
	{
		shadowPosition = mul(u_pointShadowMatrices[1], worldPosition);
	}
	else if (faceIndex == 2)
	{
		shadowPosition = mul(u_pointShadowMatrices[2], worldPosition);
	}
	else if (faceIndex == 3)
	{
		shadowPosition = mul(u_pointShadowMatrices[3], worldPosition);
	}
	else if (faceIndex == 4)
	{
		shadowPosition = mul(u_pointShadowMatrices[4], worldPosition);
	}
	else if (faceIndex == 5)
	{
		shadowPosition = mul(u_pointShadowMatrices[5], worldPosition);
	}
	return shadowPosition;
}

float samplePointLightShadowFace(int faceIndex, vec3 shadowCoord)
{
	float visibility = shadow2D(s_pointShadowPosX, shadowCoord);
	if (faceIndex == 1)
	{
		visibility = shadow2D(s_pointShadowNegX, shadowCoord);
	}
	else if (faceIndex == 2)
	{
		visibility = shadow2D(s_pointShadowPosY, shadowCoord);
	}
	else if (faceIndex == 3)
	{
		visibility = shadow2D(s_pointShadowNegY, shadowCoord);
	}
	else if (faceIndex == 4)
	{
		visibility = shadow2D(s_pointShadowPosZ, shadowCoord);
	}
	else if (faceIndex == 5)
	{
		visibility = shadow2D(s_pointShadowNegZ, shadowCoord);
	}
	return visibility;
}

float samplePointLightShadow(int pointLightIndex, vec3 worldPosition, vec3 worldNormal)
{
	if (u_pointShadowParams.w < 0.5)
	{
		return 1.0;
	}

	vec3 fragmentFromLight = worldPosition - u_pointLightPosRadius[pointLightIndex].xyz;
	vec3 absoluteOffset = abs(fragmentFromLight);
	int faceIndex = 0;
	if (absoluteOffset.x >= absoluteOffset.y && absoluteOffset.x >= absoluteOffset.z)
	{
		faceIndex = fragmentFromLight.x >= 0.0 ? 0 : 1;
	}
	else if (absoluteOffset.y >= absoluteOffset.z)
	{
		faceIndex = fragmentFromLight.y >= 0.0 ? 2 : 3;
	}
	else
	{
		faceIndex = fragmentFromLight.z >= 0.0 ? 4 : 5;
	}

	vec3 biasedWorldPosition = worldPosition
		+ worldNormal * max(u_pointShadowParams.y, 0.0);
	vec4 shadowVarying = pointLightShadowVarying(
		faceIndex, vec4(biasedWorldPosition, 1.0));
	if (shadowVarying.w <= 0.0)
	{
		return 1.0;
	}
	vec3 shadowCoord = shadowVarying.xyz / shadowVarying.w;
	bool outside = any(lessThan(shadowCoord.xy, vec2_splat(0.0)))
		|| any(greaterThan(shadowCoord.xy, vec2_splat(1.0)))
		|| shadowCoord.z < 0.0 || shadowCoord.z > 1.0;
	if (outside)
	{
		return 1.0;
	}

	float compareDepth = shadowCoord.z - max(u_pointShadowParams.x, 0.0);
	vec2 texel = vec2_splat(u_pointShadowParams.z);
	vec2 sampleMinimum = texel * 0.5;
	vec2 sampleMaximum = vec2_splat(1.0) - sampleMinimum;
	float visibility = 0.0;
	for (int sampleY = -1; sampleY <= 1; ++sampleY)
	{
		for (int sampleX = -1; sampleX <= 1; ++sampleX)
		{
			vec2 sampleUv = clamp(
				shadowCoord.xy + texel * vec2(float(sampleX), float(sampleY)),
				sampleMinimum, sampleMaximum);
			visibility += samplePointLightShadowFace(
				faceIndex, vec3(sampleUv, compareDepth));
		}
	}
	return visibility / 9.0;
}

void main()
{
	vec4 texel = texture2D(s_texColor, v_texcoord0);
	vec4 baseColor = vec4(v_color0.rgb * srgbToLinear(texel.rgb), v_color0.a * texel.a);

	float metallic = clamp(u_mrParams.x, 0.0, 1.0);
	float roughness = clamp(u_mrParams.y, 0.04, 1.0);
	if (u_mrParams.w > 0.5)
	{
		vec4 mrSample = texture2D(s_texMR, v_texcoord0);
		// glTF metallic-roughness texture: G roughness, B metallic.
		roughness = clamp(mrSample.g * u_mrParams.y, 0.04, 1.0);
		metallic = clamp(mrSample.b * u_mrParams.x, 0.0, 1.0);
	}

	vec3 geometricNormal = safeNormalize(v_normal);
	vec3 N = geometricNormal;
	if (u_normalParams.x > 0.5)
	{
		vec3 T = safeNormalize(v_tangent.xyz - N * dot(N, v_tangent.xyz));
		float tangentHandedness = v_tangent.w < 0.0 ? -1.0 : 1.0;
		vec3 B = cross(N, T) * tangentHandedness;
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
			float visibility = 1.0;
			if (abs(u_csmParams.w - float(lightIndex + 1)) < 0.5)
			{
				visibility = sampleCascadedDirectionalShadow(v_worldPos, geometricNormal);
			}
			lit += visibility * shadeLight(N, V, safeNormalize(u_lightDirs[lightIndex].xyz),
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
				float visibility = 1.0;
				if (abs(u_pointShadowParams.w - float(pointLightIndex + 1)) < 0.5)
				{
					visibility = samplePointLightShadow(
						pointLightIndex, v_worldPos, geometricNormal);
				}
				lit += visibility * shadeLight(N, V, safeNormalize(lightOffset),
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
					float visibility = 1.0;
					if (abs(u_spotShadowParams.w - float(spotLightIndex + 1)) < 0.5)
					{
						visibility = sampleSpotLightShadow(v_worldPos, geometricNormal);
					}
					lit += visibility * shadeLight(N, V, safeNormalize(-fragmentFromLight),
						u_spotLightColorOuter[spotLightIndex].rgb * attenuation,
					albedo, F0, metallic, roughness);
			}
		}
	}

	if (u_iblParams.z > 0.5)
	{
		lit += shadeImageBasedLighting(N, V, albedo, F0, metallic, roughness);
	}
	else
	{
		float ambientScale = max(u_mrParams.z, 0.0);
		lit += albedo * ambientScale * (1.0 - metallic * 0.6) + F0 * (ambientScale * 0.25);
	}

	gl_FragColor = vec4(linearToSrgb(lit), baseColor.a);
}
