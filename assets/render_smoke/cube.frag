#version 410 core

in vec3 worldNormal;
in vec3 faceColor;
in vec2 faceUV;
in vec3 worldPosition;
layout(std140) uniform ViewConstants {
    mat4 view;
    mat4 projection;
    vec4 ambientLight;
    vec4 lightDirection;
    vec4 lightColor;
    mat4 lightViewProjection;
};
uniform sampler2D albedoTexture;
uniform sampler2DShadow shadowMap;
layout(location = 0) out vec4 color;

void main()
{
    float diffuse = max(dot(normalize(worldNormal), normalize(lightDirection.xyz)), 0.0);
    // The white texture preserves cube color while the floor samples its
    // independently cooked blockout image through the same shader slot.
    vec3 albedo = texture(albedoTexture, faceUV).rgb;
    // This fixed light uses one shadow map. Fragments outside its coverage
    // remain lit rather than clamping to an unrelated border texel.
    float visibility = 1.0;
    vec4 lightClip = lightViewProjection * vec4(worldPosition, 1.0);
    if (lightClip.w > 0.0) {
        vec3 lightNdc = lightClip.xyz / lightClip.w;
        if (all(lessThanEqual(abs(lightNdc), vec3(1.0)))) {
            vec2 shadowUV = lightNdc.xy * 0.5 + 0.5;
            float referenceDepth = lightNdc.z * 0.5 + 0.5 - 0.0025;
            visibility = texture(shadowMap, vec3(shadowUV, referenceDepth));
        }
    }
    vec3 shaded = faceColor * albedo *
        (ambientLight.rgb + lightColor.rgb * diffuse * visibility);
    color = vec4(shaded, 1.0);
}
