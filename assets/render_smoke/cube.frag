#version 410 core

in vec3 worldNormal;
in vec3 faceColor;
in vec2 faceUV;
uniform sampler2D albedoTexture;
layout(location = 0) out vec4 color;

void main()
{
    vec3 lightDirection = normalize(vec3(0.4, -0.7, 1.0));
    float diffuse = max(dot(normalize(worldNormal), lightDirection), 0.0);
    // The white texture preserves cube color while the floor samples its
    // independently cooked blockout image through the same shader slot.
    vec3 albedo = texture(albedoTexture, faceUV).rgb;
    vec3 shaded = faceColor * albedo * (0.30 + 0.70 * diffuse);
    color = vec4(shaded, 1.0);
}
