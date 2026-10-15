#version 410 core
// Default Model112; CY_WORLD_CUTOUT opts into the 128-byte Model record.
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
    vec4 fogColor;
    vec4 fogDepths;
};
layout(std140) uniform Model {
    mat4 model;
    mat3 normalFromLocal;
#ifdef CY_WORLD_CUTOUT
    vec4 cutout;
#endif
};
out vec3 worldPosition;
out vec3 worldNormal;
out vec2 surfaceUV;
out vec4 lightClipPosition;
out float cameraDepth;
#ifdef CY_WORLD_CUTOUT
flat out float cutoutThreshold;
#endif
void main() {
    vec4 world = model * vec4(position, 1.0);
    worldPosition = world.xyz;
    worldNormal = normalFromLocal * normal;
    surfaceUV = uv;
#ifdef CY_WORLD_CUTOUT
    cutoutThreshold = cutout.x;
#endif
    lightClipPosition = lightClipFromWorld * world;
    vec4 camera = view * world;
    cameraDepth = -camera.z;
    gl_Position = projection * camera;
}
