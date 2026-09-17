#version 410 core

layout(location = 3) in vec2 debugUV;

layout(location = 0) out vec4 color;

void main()
{
    vec2 localUV = fract(debugUV);
    vec2 edgeDistance = min(localUV, vec2(1.0) - localUV);
    float nearestEdge = min(edgeDistance.x, edgeDistance.y);
    float edgeWidth = max(max(fwidth(debugUV.x), fwidth(debugUV.y)), 0.0005);
    float tileBoundary = 1.0 - smoothstep(0.0, edgeWidth * 1.5, nearestEdge);

    vec2 cell = floor(debugUV);
    float parity = mod(cell.x + cell.y, 2.0);
    vec3 uvColor = vec3(localUV, 0.20 + 0.16 * parity);

    // White cell borders expose discontinuities, wrapping, and mirrored UVs.
    vec3 result = mix(uvColor, vec3(1.0), tileBoundary);
    color = vec4(result, 1.0);
}
