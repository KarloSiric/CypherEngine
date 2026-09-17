#version 410 core
in vec3 worldNormal;
in vec4 surfaceTint;
in vec2 surfaceUV;
uniform sampler2D base_color;
layout(location = 0) out vec4 color;
void main() {
    vec3 lightDirection = normalize(vec3(0.4, -0.7, 1.0));
    float diffuse = max(dot(normalize(worldNormal), lightDirection), 0.0);
    vec3 linearColor = texture(base_color, surfaceUV).rgb * surfaceTint.rgb * (0.30 + 0.70 * diffuse);
    // Editor and runtime presentation targets currently disable framebuffer sRGB.
    vec3 encoded = mix(linearColor * 12.92, 1.055 * pow(max(linearColor, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055,
                       greaterThan(linearColor, vec3(0.0031308)));
    color = vec4(encoded, 1.0);
}
