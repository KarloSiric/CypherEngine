#version 410 core

in vec3 worldNormal;
in vec3 faceColor;
in vec2 faceUV;
layout(location = 0) out vec4 color;

void main()
{
    vec3 lightDirection = normalize(vec3(0.4, -0.7, 1.0));
    float diffuse = max(dot(normalize(worldNormal), lightDirection), 0.0);
    // UV grid lines make all three vertex attributes visible without textures.
    vec2 grid = abs(fract(faceUV * 4.0 - 0.5) - 0.5);
    vec2 lineWidth = max(fwidth(faceUV * 4.0), vec2(0.001));
    float line = 1.0 - min(smoothstep(vec2(0.0), lineWidth, grid).x,
                           smoothstep(vec2(0.0), lineWidth, grid).y);
    vec3 shaded = faceColor * (0.30 + 0.70 * diffuse);
    color = vec4(mix(shaded, shaded * 0.45, line * 0.7), 1.0);
}
