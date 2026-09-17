#version 410 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;

// OpenGL 4.1 assigns this block's binding through the renderer pipeline.
layout(std140) uniform Transforms {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 tint;
};

out vec3 worldNormal;
out vec3 faceColor;
out vec2 faceUV;

void main()
{
    gl_Position = projection * view * model * vec4(position, 1.0);
    // The demo uses rotation only, so an inverse-transpose is unnecessary.
    worldNormal = mat3(model) * normal;
    float faceTone = mix(0.72, 1.12, 0.5 + 0.5 * normal.z);
    faceColor = tint.rgb * faceTone;
    faceUV = texcoord;
}
