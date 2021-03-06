#version 300 es

precision mediump float;

// Interpolated values from the vertex shader
in vec2 UV;
in vec4 vertexPos_world;

// Ouput data
out vec4 color;

// Values that stay constant for the whole mesh
uniform vec4 fragOffset;
uniform sampler2D DiffuseTextureSampler;

void main()
{
  color = texture(DiffuseTextureSampler, UV) + fragOffset;
  color.r += 0.2;
  color.g += 0.2;
}

// vim: syntax=glsl
