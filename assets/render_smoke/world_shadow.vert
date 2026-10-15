#version 410 core
layout(location = 0) in vec3 position;
#ifdef CY_WORLD_CUTOUT
layout(location = 2) in vec2 uv;
#endif
layout(std140) uniform Light { mat4 lightClipFromWorld; };
layout(std140) uniform Model {
    mat4 model;
#ifdef CY_WORLD_CUTOUT
    vec4 cutout;
#endif
};
#ifdef CY_WORLD_CUTOUT
out vec2 surfaceUV;
flat out float cutoutThreshold;
#endif
void main() {
#ifdef CY_WORLD_CUTOUT
    surfaceUV = uv;
    cutoutThreshold = cutout.x;
#endif
    gl_Position = lightClipFromWorld * model * vec4(position, 1.0);
}
