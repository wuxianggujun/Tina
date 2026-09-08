#ifndef TINA_SKIN_PALETTE_SH
#define TINA_SKIN_PALETTE_SH

// Reflection encodes array counts in a byte; joint 255 uses its own uniform.
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

mat4 tinaSkinMatrix(ivec4 joints, vec4 weights)
{
    return tinaSkinJointMatrix(joints.x) * weights.x +
           tinaSkinJointMatrix(joints.y) * weights.y +
           tinaSkinJointMatrix(joints.z) * weights.z +
           tinaSkinJointMatrix(joints.w) * weights.w;
}

#endif
