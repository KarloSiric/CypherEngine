#version 410 core

layout(location = 0) out vec4 color;

uint HashPrimitiveID(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

void main()
{
    uint hashed = HashPrimitiveID(uint(gl_PrimitiveID) + 1u);
    vec3 palette = vec3(
        float(hashed & 0xffu),
        float((hashed >> 8u) & 0xffu),
        float((hashed >> 16u) & 0xffu)) / 255.0;

    // Keep every generated channel away from near-black and near-white so
    // adjacent primitive colors stay legible against common editor themes.
    color = vec4(mix(vec3(0.16), vec3(0.90), palette), 1.0);
}
