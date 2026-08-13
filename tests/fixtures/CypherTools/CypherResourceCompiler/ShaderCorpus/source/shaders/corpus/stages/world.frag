#version 410 core
in vec3 vWorldNormal;
in vec2 vTexCoord;
in vec4 vTint;
in float vFogDepth;

uniform sampler2D uBaseColor;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 surface = texture(uBaseColor, vTexCoord) * vTint;
#if defined(CY_ALPHA_TEST)
    if (surface.a < 0.5) {
        discard;
    }
#endif
    vec3 lightDirection = normalize(vec3(-0.35, -0.7, -0.4));
    float diffuse = max(dot(normalize(vWorldNormal), -lightDirection), 0.0);
#if defined(CY_UNLIT)
    vec3 lighting = vec3(1.0);
#else
    vec3 lighting = vec3(0.18 + diffuse * 0.82);
#endif
#if defined(CY_USE_EMISSIVE)
    lighting += vec3(0.15, 0.06, 0.02);
#endif
    vec3 color = surface.rgb * lighting;
#if defined(CY_USE_FOG)
    float fog = 1.0 - exp(-0.025 * vFogDepth);
    color = mix(color, vec3(0.08, 0.11, 0.12), clamp(fog, 0.0, 1.0));
#endif
    outColor = vec4(color, surface.a);
}
