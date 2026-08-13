#version 410 core
in vec2 vTexCoord;

uniform sampler2D uSceneColor;

layout(location = 0) out vec4 outColor;

vec3 Tonemap(vec3 color)
{
#if defined(CY_REINHARD_TONEMAP)
    return color / (color + vec3(1.0));
#else
    return vec3(1.0) - exp(-color);
#endif
}

void main()
{
    vec2 uv = vTexCoord;
    const vec2 inverseResolution = vec2(1.0 / 1280.0, 1.0 / 720.0);
#if defined(CY_PIXELATE)
    uv = floor(uv / (inverseResolution * 4.0)) *
         (inverseResolution * 4.0);
#endif
    vec3 color = texture(uSceneColor, uv).rgb;
#if defined(CY_SCANLINES)
    color *= 0.92 + 0.08 * sin(
        (uv.y / inverseResolution.y) * 3.14159265);
#endif
#if defined(CY_VIGNETTE)
    vec2 centered = uv * 2.0 - 1.0;
    color *= 1.0 - 0.28 * dot(centered, centered);
#endif
    outColor = vec4(Tonemap(max(color, vec3(0.0))), 1.0);
}
