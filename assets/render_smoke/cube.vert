#version 410 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;

// OpenGL 4.1 assigns both block bindings through the renderer pipeline.
layout(std140) uniform ViewConstants {
    mat4 view;
    mat4 projection;
    vec4 ambientLight;
    vec4 lightDirection;
    vec4 lightColor;
    mat4 lightViewProjection;
    vec4 pointLightPositionRange[2];
    vec4 pointLightColorIntensity[2];
    ivec4 pointLightControl;
};

layout(std140) uniform ObjectConstants {
    mat4 model;
    vec4 tint;
    mat3 normalMatrix;
    vec4 cutout;
};

out vec3 worldNormal;
out vec4 faceColor;
out vec2 faceUV;
out vec3 worldPosition;

void main()
{
    vec4 worldPoint = model * vec4(position, 1.0);
    gl_Position = projection * view * worldPoint;
    worldNormal = normalMatrix * normal;
    float faceTone = mix(0.72, 1.12, 0.5 + 0.5 * normal.z);
    faceColor = vec4(tint.rgb * faceTone, tint.a);
    faceUV = texcoord;
    worldPosition = worldPoint.xyz;
}
