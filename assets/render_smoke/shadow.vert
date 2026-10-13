#version 410 core

layout(location = 0) in vec3 position;

layout(std140) uniform LightConstants {
    mat4 lightViewProjection;
};

layout(std140) uniform ObjectConstants {
    mat4 model;
};

void main()
{
    gl_Position = lightViewProjection * model * vec4(position, 1.0);
}
