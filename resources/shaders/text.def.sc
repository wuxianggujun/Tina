// 专用 text 着色器的 varying/attribute 映射，避免与其它着色器互相影响

// Varyings（VS -> FS）
vec4 v_color0    : COLOR0;
vec2 v_texcoord0 : TEXCOORD0;

// Attributes（顶点输入）
vec3 a_position  : POSITION;
vec4 a_color0    : COLOR0;
vec2 a_texcoord0 : TEXCOORD0;

