#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;

out vec3 vWorldNormal;
out vec2 vTexCoord;
out vec4 vTint;
out float vFogDepth;

void main()
{
    vec4 worldPosition = vec4(aPosition, 1.0);
    vWorldNormal = normalize(aNormal);
    vTexCoord = aTexCoord;
#if defined(CY_FLIP_UV)
    vTexCoord.y = 1.0 - vTexCoord.y;
#endif
#if defined(CY_USE_VERTEX_COLOR)
    vTint = aColor;
#else
    vTint = vec4(1.0);
#endif
    vFogDepth = abs(worldPosition.z);
    gl_Position = worldPosition;
}
