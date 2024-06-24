$input a_position, a_color0
$output v_color0

#include <bgfx_shader.sh>


void main()
{
  float xScale = u_viewRect.w / u_viewRect.z;
  float yScale = u_viewRect.z / u_viewRect.w;

  gl_Position = vec4(
    a_position.x * (u_viewRect.w > u_viewRect.z ? 1.0 : xScale), // Use scaleFactor if width > z
    a_position.y * (u_viewRect.w < u_viewRect.z ? 1.0 : yScale), // Use yScale if width <= z
    0.0, 1.0);

  v_color0 = a_color0;
}
