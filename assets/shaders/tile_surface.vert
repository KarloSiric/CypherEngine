#version 410 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;
layout(std140) uniform Transforms {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 tint;
    vec4 uvScale;
};
out vec3 worldNormal;
out vec4 surfaceTint;
out vec2 surfaceUV;
void main() {
    gl_Position = projection * view * model * vec4(position, 1.0);
    worldNormal = normalize(transpose(inverse(mat3(model))) * normal);
    surfaceTint = tint;
    surfaceUV = texcoord * uvScale.xy;
}
