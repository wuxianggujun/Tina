#ifndef TINA_ALPHA_MASK_SH
#define TINA_ALPHA_MASK_SH

// x = Mask, y = alphaCutoff, z = Blend, w = linear offscreen output.
uniform vec4 u_alphaParams;

void tinaApplyAlphaMask(float alpha)
{
    if (u_alphaParams.x > 0.5 && alpha < u_alphaParams.y)
    {
        discard;
    }
}

#endif
