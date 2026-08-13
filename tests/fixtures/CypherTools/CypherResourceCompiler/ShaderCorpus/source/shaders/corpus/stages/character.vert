#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in uvec4 aBoneIndices;
layout(location = 4) in vec4 aBoneWeights;

out vec3 vWorldNormal;
out vec2 vTexCoord;
out float vRimFactor;

void main()
{
    vec3 localPosition = aPosition;
#if defined(CY_SKINNED)
    float weightedIndex = dot(
        vec4(aBoneIndices),
        aBoneWeights);
    localPosition += aNormal * fract(weightedIndex * 0.03125) * 0.015;
#endif
    vWorldNormal = normalize(aNormal);
    vTexCoord = aTexCoord;
    vRimFactor = 1.0 - abs(vWorldNormal.z);
    gl_Position = vec4(localPosition, 1.0);
}
