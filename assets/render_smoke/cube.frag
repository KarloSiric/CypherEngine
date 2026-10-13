#version 410 core

in vec3 worldNormal;
in vec3 faceColor;
in vec2 faceUV;
layout(std140) uniform ViewConstants {
    mat4 view;
    mat4 projection;
    vec4 ambientLight;
    vec4 lightDirection;
    vec4 lightColor;
};
uniform sampler2D albedoTexture;
layout(location = 0) out vec4 color;

void main()
{
    float diffuse = max(dot(normalize(worldNormal), normalize(lightDirection.xyz)), 0.0);
    // The white texture preserves cube color while the floor samples its
    // independently cooked blockout image through the same shader slot.
    vec3 albedo = texture(albedoTexture, faceUV).rgb;
    vec3 shaded = faceColor * albedo * (ambientLight.rgb + lightColor.rgb * diffuse);
    color = vec4(shaded, 1.0);
}
