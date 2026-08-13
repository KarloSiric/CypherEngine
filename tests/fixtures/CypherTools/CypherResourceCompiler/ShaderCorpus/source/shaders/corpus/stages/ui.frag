#version 410 core
in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uAtlas;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 sampled = texture(uAtlas, vTexCoord);
#if defined(CY_UI_MONOCHROME)
    sampled.rgb = vec3(sampled.r);
#endif
#if defined(CY_UI_PREMULTIPLIED)
    sampled.rgb *= sampled.a;
#endif
#if defined(CY_UI_CLIP_ALPHA)
    const vec4 clipRect = vec4(0.0, 0.0, 1920.0, 1080.0);
    sampled.a *= step(clipRect.x, gl_FragCoord.x) *
                 step(clipRect.y, gl_FragCoord.y) *
                 step(gl_FragCoord.x, clipRect.z) *
                 step(gl_FragCoord.y, clipRect.w);
#endif
    outColor = sampled * vColor;
}
