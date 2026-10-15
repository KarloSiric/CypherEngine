#version 410 core
layout(std140) uniform View {
    mat4 view;
    mat4 projection;
    vec4 ambientLight;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 pointLightPositionRange[2];
    vec4 pointLightColorIntensity[2];
    ivec4 pointLightControl;
    mat4 lightClipFromWorld;
    vec4 fogColor;
    vec4 fogDepths;
};
layout(std140) uniform Material { vec4 tint; };
uniform sampler2D albedoMap;
uniform sampler2DShadow shadowMap;
in vec3 worldPosition;
in vec3 worldNormal;
in vec2 surfaceUV;
in vec4 lightClipPosition;
in float cameraDepth;
out vec4 color;

float FogAmount() {
    if (fogDepths.z == 0.0) return 0.0;
    float depth = max(cameraDepth, 0.0);
    if (depth <= fogDepths.x) return 0.0;
    if (depth >= fogDepths.y) return 1.0;
    return (depth - fogDepths.x) / (fogDepths.y - fogDepths.x);
}

float DirectionalVisibility() {
    if (lightClipPosition.w <= 0.0) return 1.0;
    vec3 projected = lightClipPosition.xyz / lightClipPosition.w * 0.5 + 0.5;
    if (any(lessThan(projected, vec3(0.0))) ||
        any(greaterThan(projected, vec3(1.0)))) return 1.0;
    // The depth pass also uses slope bias. Comparison filtering is selected
    // by the host's sampler, independently of the material's albedo sampler.
    return texture(shadowMap, vec3(projected.xy, projected.z - 0.0005));
}

void main() {
    vec3 n = normalize(worldNormal);
    vec3 irradiance = ambientLight.rgb + lightColor.rgb *
        max(dot(n, lightDirection.xyz), 0.0) * DirectionalVisibility();
    for (int i = 0; i < min(pointLightControl.x, 2); ++i) {
        vec3 toLight = pointLightPositionRange[i].xyz - worldPosition;
        float distanceToLight = length(toLight);
        float range = pointLightPositionRange[i].w;
        float attenuation = max(1.0 - distanceToLight / range, 0.0);
        vec3 direction = toLight / max(distanceToLight, 0.00001);
        irradiance += pointLightColorIntensity[i].rgb *
            pointLightColorIntensity[i].w * attenuation * attenuation *
            max(dot(n, direction), 0.0);
    }
    vec4 albedo = texture(albedoMap, surfaceUV) * tint;
    // Standard alpha surfaces blend this fogged source over the HDR scene.
    // Additive surfaces contribute only extinguished radiance: mixing in the
    // fog color would add that color once per overlapping additive draw.
#ifdef CY_WORLD_ADDITIVE_FOG
    color = vec4(albedo.rgb * irradiance * (1.0 - FogAmount()), albedo.a);
#else
    color = vec4(mix(albedo.rgb * irradiance, fogColor.rgb, FogAmount()), albedo.a);
#endif
}
