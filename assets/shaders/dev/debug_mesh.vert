#version 410 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;

// Matches the mesh transform block currently uploaded by CypherRender and the
// TileEditor preview path. uvScale.zw is reserved by that runtime contract.
layout(std140) uniform Transforms {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 tint;
    vec4 uvScale;
};

layout(location = 0) out vec3 debugObjectNormal;
layout(location = 1) out vec3 debugWorldNormal;
layout(location = 2) out vec3 debugWorldPosition;
layout(location = 3) out vec2 debugUV;
layout(location = 4) out vec4 debugTint;

void main()
{
    vec4 worldPosition = model * vec4(position, 1.0);
    gl_Position = projection * view * worldPosition;

    debugObjectNormal = normalize(normal);
    debugWorldNormal = normalize(transpose(inverse(mat3(model))) * normal);
    debugWorldPosition = worldPosition.xyz;
    debugUV = texcoord * uvScale.xy;
    debugTint = tint;
}
