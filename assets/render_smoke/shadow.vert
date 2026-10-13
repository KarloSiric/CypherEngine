#version 410 core

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texcoord;

layout(std140) uniform LightConstants {
    mat4 lightViewProjection;
};

layout(std140) uniform ObjectConstants {
    mat4 model;
    vec4 tint;
    vec4 cutout;
};

out vec2 faceUV;

void main()
{
    gl_Position = lightViewProjection * model * vec4(position, 1.0);
    faceUV = texcoord;
}
