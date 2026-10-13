#version 410 core

in vec3 worldNormal;
in vec4 faceColor;
in vec2 faceUV;
in vec3 worldPosition;
layout(std140) uniform ViewConstants {
    mat4 view;
    mat4 projection;
    vec4 ambientLight;
    vec4 lightDirection;
    vec4 lightColor;
    mat4 lightViewProjection;
    vec4 pointLightPositionRange[2];
    vec4 pointLightColorIntensity[2];
    ivec4 pointLightControl;
};
layout(std140) uniform ObjectConstants {
    mat4 model;
    vec4 tint;
    mat3 normalMatrix;
    vec4 cutout;
};
uniform sampler2D albedoTexture;
uniform sampler2DShadow shadowMap;
layout(location = 0) out vec4 color;

void main()
{
    vec3 surfaceNormal = normalize(worldNormal);
    float diffuse = max(dot(surfaceNormal, normalize(lightDirection.xyz)), 0.0);
    // The white texture preserves cube color while the floor samples its
    // independently cooked blockout image through the same shader slot.
    vec4 albedo = texture(albedoTexture, faceUV);
    if (albedo.a < cutout.x) discard;
    // This fixed light uses one shadow map. Fragments outside its coverage
    // remain lit rather than clamping to an unrelated border texel.
    float visibility = 1.0;
    vec4 lightClip = lightViewProjection * vec4(worldPosition, 1.0);
    if (lightClip.w > 0.0) {
        vec3 lightNdc = lightClip.xyz / lightClip.w;
        if (all(lessThanEqual(abs(lightNdc), vec3(1.0)))) {
            vec2 shadowUV = lightNdc.xy * 0.5 + 0.5;
            float referenceDepth = lightNdc.z * 0.5 + 0.5 - 0.00025;
            vec2 shadowTexel = 1.0 / vec2(textureSize(shadowMap, 0));
            float litTaps = 0.0;
            for (int y = -1; y <= 1; ++y) {
                for (int x = -1; x <= 1; ++x) {
                    vec2 tapUV = shadowUV + vec2(x, y) * shadowTexel;
                    // Border taps are lit even when the depth sampler clamps.
                    if (all(greaterThanEqual(tapUV, vec2(0.0))) &&
                        all(lessThanEqual(tapUV, vec2(1.0)))) {
                        litTaps += texture(shadowMap, vec3(tapUV, referenceDepth));
                    } else {
                        litTaps += 1.0;
                    }
                }
            }
            visibility = litTaps / 9.0;
        }
    }
    vec3 pointLighting = vec3(0.0);
    for (int lightIndex = 0; lightIndex < 2; ++lightIndex) {
        if (lightIndex >= pointLightControl.x) break;

        float range = pointLightPositionRange[lightIndex].w;
        vec3 toLight = pointLightPositionRange[lightIndex].xyz - worldPosition;
        float distanceToLight = length(toLight);
        if (range <= 0.0 || distanceToLight <= 0.0001 || distanceToLight >= range) continue;

        float lambert = max(dot(surfaceNormal, toLight / distanceToLight), 0.0);
        float remainingRange = 1.0 - distanceToLight / range;
        float falloff = remainingRange * remainingRange;
        pointLighting += pointLightColorIntensity[lightIndex].rgb *
            (pointLightColorIntensity[lightIndex].a * lambert * falloff);
    }

    vec3 shaded = faceColor.rgb * albedo.rgb *
        (ambientLight.rgb + lightColor.rgb * diffuse * visibility + pointLighting);
    color = vec4(shaded, faceColor.a * albedo.a);
}
