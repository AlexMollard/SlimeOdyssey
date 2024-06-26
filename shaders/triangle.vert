#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) out vec3 fragColor;

vec2 positions[3] = vec2[](vec2 (0.0, -0.5), vec2 (0.5, 0.5), vec2 (-0.5, 0.5));

vec3 colors[3] = vec3[](
	vec3(1.0, 0.7, 0.5),   // Pastel orange
	vec3(0.7, 1.0, 0.7),   // Pastel green
	vec3(0.7, 0.7, 1.0)    // Pastel blue
);

void main ()
{
	gl_Position = vec4 (positions[gl_VertexIndex], 0.0, 1.0);
	fragColor = colors[gl_VertexIndex];
}