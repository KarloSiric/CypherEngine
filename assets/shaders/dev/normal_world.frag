#version 410 core

layout(location = 1) in vec3 debugWorldNormal;

layout(location = 0) out vec4 color;

void main()
{
    vec3 encodedNormal = normalize(debugWorldNormal) * 0.5 + 0.5;
    color = vec4(encodedNormal, 1.0);
}
