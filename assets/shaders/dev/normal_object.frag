#version 410 core

layout(location = 0) in vec3 debugObjectNormal;

layout(location = 0) out vec4 color;

void main()
{
    vec3 encodedNormal = normalize(debugObjectNormal) * 0.5 + 0.5;
    color = vec4(encodedNormal, 1.0);
}
