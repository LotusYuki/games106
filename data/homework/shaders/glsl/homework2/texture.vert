#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUV;

layout (set = 0, binding = 0) uniform UBO 
{
	mat4 projection;
    mat4 view;
	mat4 model;
    vec4 lightPos;
	vec4 viewPos;
	int colorShadingRates;
} ubo;

layout (location = 0) out vec2 outUV;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() 
{
	outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(outUV * 2.0f - 1.0f, 0.0f, 1.0f);
}