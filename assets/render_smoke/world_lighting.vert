#version 410 core
// Matches the public World View320, Model112 and Material16 contracts.
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(std140) uniform View {
    mat4 view;
    mat4 projection;
    vec4 ambientLight;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 pointLightPositionRange[2];
    vec4 pointLightColorIntensity[2];
    ivec4 pointLightControl;
    mat4 lightClipFromWorld;
};
layout(std140) uniform Model { mat4 model; mat3 normalFromLocal; };
out vec3 worldPosition;
out vec3 worldNormal;
out vec2 surfaceUV;
out vec4 lightClipPosition;
void main() {
    vec4 world = model * vec4(position, 1.0);
    worldPosition = world.xyz;
    worldNormal = normalFromLocal * normal;
    surfaceUV = uv;
    lightClipPosition = lightClipFromWorld * world;
    gl_Position = projection * view * world;
}
