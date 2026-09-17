#version 410 core

layout(location = 3) in vec2 debugUV;

layout(location = 0) out vec4 color;

void main()
{
    const float cellsPerTile = 8.0;
    vec2 cellUV = debugUV * cellsPerTile;
    vec2 localCell = fract(cellUV);
    vec2 cellIndex = floor(cellUV);

    float parity = mod(cellIndex.x + cellIndex.y, 2.0);
    vec3 darkCell = vec3(0.055, 0.065, 0.080);
    vec3 lightCell = vec3(0.72, 0.76, 0.82);
    vec3 result = mix(darkCell, lightCell, parity);

    vec2 minorDistance = min(localCell, vec2(1.0) - localCell);
    float nearestMinorEdge = min(minorDistance.x, minorDistance.y);
    float minorWidth = max(max(fwidth(cellUV.x), fwidth(cellUV.y)), 0.001);
    float minorLine = 1.0 - smoothstep(0.0, minorWidth, nearestMinorEdge);
    result = mix(result, vec3(0.24), minorLine * 0.45);

    vec2 tileUV = fract(debugUV);
    vec2 majorDistance = min(tileUV, vec2(1.0) - tileUV);
    float nearestMajorEdge = min(majorDistance.x, majorDistance.y);
    float majorWidth = max(max(fwidth(debugUV.x), fwidth(debugUV.y)), 0.0005);
    float majorLine = 1.0 - smoothstep(0.0, majorWidth * 1.75, nearestMajorEdge);
    result = mix(result, vec3(0.04, 0.82, 0.95), majorLine);

    color = vec4(result, 1.0);
}
