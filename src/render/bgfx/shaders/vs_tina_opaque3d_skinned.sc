$input a_position, a_normal, a_tangent, a_texcoord0, a_indices, a_weight
$output v_color0, v_texcoord0, v_normal, v_worldPos, v_tangent

#include <bgfx_shader.sh>

// 3D-SKIN-001 A3: CPU-evaluated globalPose * inverseBind palette, one draw per
// skinned item. Shares fs_tina_opaque3d_mr; the model matrix arrives through
// bgfx setTransform (u_model[0]) and the color factor through u_tinaSkinColor.
// bgfx shader binary v11 stores a reflected uniform array length in one byte.
// Keep joints 0..254 in the array and joint 255 in a separate uniform so the
// frozen SkinnedMesh v1 bound remains 256 without encoding 256 as zero.
uniform mat4 u_tinaSkinPalette[255];
uniform mat4 u_tinaSkinPaletteLast;
uniform vec4 u_tinaSkinColor;

mat4 tinaSkinJointMatrix(int joint)
{
	mat4 result = u_tinaSkinPaletteLast;
	if (joint < 255)
	{
		result = u_tinaSkinPalette[joint];
	}
	return result;
}

void main()
{
	// BLENDINDICES must stay integer-typed for the Uint8 vertex stream. A float
	// declaration loses non-zero joint indices on D3D11's UINT input format.
	ivec4 joints = ivec4(a_indices);
	mat4 skin =
		tinaSkinJointMatrix(joints.x) * a_weight.x +
		tinaSkinJointMatrix(joints.y) * a_weight.y +
		tinaSkinJointMatrix(joints.z) * a_weight.z +
		tinaSkinJointMatrix(joints.w) * a_weight.w;
	mat4 combined = mul(u_model[0], skin);

	vec4 worldPosition = mul(combined, vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, worldPosition);
	// Product slice assumes near-uniform scale; no inverse-transpose normal matrix.
	v_normal = normalize(mul(combined, vec4(a_normal, 0.0)).xyz);
	vec3 worldTangent = mul(combined, vec4(a_tangent.xyz, 0.0)).xyz;
	float tangentLengthSquared = dot(worldTangent, worldTangent);
	worldTangent *= inversesqrt(max(tangentLengthSquared, 0.00000001));
	// Basis transform determinant sign corrects handedness under reflection;
	// computed via mul() so column/row conventions stay backend-neutral.
	vec3 basisX = mul(combined, vec4(1.0, 0.0, 0.0, 0.0)).xyz;
	vec3 basisY = mul(combined, vec4(0.0, 1.0, 0.0, 0.0)).xyz;
	vec3 basisZ = mul(combined, vec4(0.0, 0.0, 1.0, 0.0)).xyz;
	float combinedHandedness = dot(cross(basisX, basisY), basisZ) < 0.0 ? -1.0 : 1.0;
	v_tangent = vec4(worldTangent, a_tangent.w * combinedHandedness);
	v_worldPos = worldPosition.xyz;
	v_color0 = u_tinaSkinColor;
	v_texcoord0 = a_texcoord0;
}
