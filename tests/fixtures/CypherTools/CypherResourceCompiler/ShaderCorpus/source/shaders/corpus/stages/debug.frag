#version 410 core
in vec4 vColor;
in float vLinearDepth;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 color = vColor;
#if defined(CY_DEBUG_DEPTH_TINT)
    float amount = clamp(vLinearDepth * 0.025, 0.0, 1.0);
    color.rgb = mix(color.rgb, vec3(0.05, 0.7, 1.0), amount);
#endif
#if defined(CY_DEBUG_XRAY)
    color.a *= 0.45;
#endif
    outColor = color;
}
