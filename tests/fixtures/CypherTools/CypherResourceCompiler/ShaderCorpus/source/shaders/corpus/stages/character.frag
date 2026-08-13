#version 410 core
in vec3 vWorldNormal;
in vec2 vTexCoord;
in float vRimFactor;

uniform sampler2D uAlbedo;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 albedo = texture(uAlbedo, vTexCoord);
#if defined(CY_ALPHA_TEST)
    if (albedo.a < 0.5) {
        discard;
    }
#endif
    vec3 keyLight = normalize(vec3(-0.25, -0.65, -0.5));
    float ndl = max(dot(normalize(vWorldNormal), -keyLight), 0.0);
    vec3 color = albedo.rgb * (0.22 + ndl * 0.78);
#if defined(CY_RIM_LIGHT)
    color += vec3(0.16, 0.3, 0.5) *
             pow(clamp(vRimFactor, 0.0, 1.0), 3.0);
#endif
#if defined(CY_DAMAGE_FLASH)
    color = mix(color, vec3(1.0, 0.05, 0.02), 0.35);
#endif
    outColor = vec4(color, albedo.a);
}
