$input a_position, a_normal, a_tangent, a_texcoord0, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_color0, v_texcoord0, v_normal, v_worldPos, v_tangent

#include <bgfx_shader.sh>

void main()
{
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	vec4 worldPosition = mul(model, vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, worldPosition);
	// Product slice assumes near-uniform scale; no inverse-transpose normal matrix.
	v_normal = normalize(mul(model, vec4(a_normal, 0.0)).xyz);
	vec3 worldTangent = mul(model, vec4(a_tangent.xyz, 0.0)).xyz;
	float tangentLengthSquared = dot(worldTangent, worldTangent);
	worldTangent *= inversesqrt(max(tangentLengthSquared, 0.00000001));
	// cross(transformed N, transformed T) changes handedness under a reflected model.
	float modelHandedness = dot(cross(i_data0.xyz, i_data1.xyz), i_data2.xyz) < 0.0 ? -1.0 : 1.0;
	v_tangent = vec4(worldTangent, a_tangent.w * modelHandedness);
	v_worldPos = worldPosition.xyz;
	v_color0 = i_data4;
	v_texcoord0 = a_texcoord0;
}
