#version 410 core

in vec2 sceneUV;
uniform sampler2D sceneColor;
layout(location = 0) out vec4 color;

float LinearToSRGB(float value)
{
    return value <= 0.0031308
        ? 12.92 * value
        : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

void main()
{
    // Fixed unit exposure and Reinhard mapping keep the sample deliberately
    // small. The presentation framebuffer's automatic sRGB conversion is off,
    // so this final pass applies the exact display transfer once in shader.
    vec3 linearHDR = max(texture(sceneColor, sceneUV).rgb, vec3(0.0));
    vec3 mapped = linearHDR / (vec3(1.0) + linearHDR);
    color = vec4(LinearToSRGB(mapped.r), LinearToSRGB(mapped.g),
        LinearToSRGB(mapped.b), 1.0);
}
