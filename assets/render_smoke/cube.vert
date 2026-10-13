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
};

layout(std140) uniform ObjectConstants {
    mat4 model;
    vec4 tint;
};

out vec3 worldNormal;
out vec3 faceColor;
out vec2 faceUV;
out vec3 worldPosition;

void main()
{
    vec4 worldPoint = model * vec4(position, 1.0);
    gl_Position = projection * view * worldPoint;
    // The demo uses rotation only, so an inverse-transpose is unnecessary.
    worldNormal = mat3(model) * normal;
    float faceTone = mix(0.72, 1.12, 0.5 + 0.5 * normal.z);
    faceColor = tint.rgb * faceTone;
    faceUV = texcoord;
    worldPosition = worldPoint.xyz;
}
