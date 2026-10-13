#version 410 core

in vec3 worldNormal;
in vec3 faceColor;
layout(location = 0) out vec4 color;

void main()
{
    vec3 lightDirection = normalize(vec3(0.4, -0.7, 1.0));
    float diffuse = max(dot(normalize(worldNormal), lightDirection), 0.0);
    color = vec4(faceColor * (0.30 + 0.70 * diffuse), 1.0);
}
