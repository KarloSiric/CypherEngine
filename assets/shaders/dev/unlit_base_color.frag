#version 410 core

layout(location = 3) in vec2 debugUV;
layout(location = 4) in vec4 debugTint;

uniform sampler2D base_color;

layout(location = 0) out vec4 color;

vec3 LinearToSrgb(vec3 linearColor)
{
    vec3 nonnegative = max(linearColor, vec3(0.0));
    vec3 low = nonnegative * 12.92;
    vec3 high = 1.055 * pow(nonnegative, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(nonnegative, vec3(0.0031308)));
}

void main()
{
    vec4 sampleColor = texture(base_color, debugUV) * debugTint;
    color = vec4(LinearToSrgb(sampleColor.rgb), sampleColor.a);
}
