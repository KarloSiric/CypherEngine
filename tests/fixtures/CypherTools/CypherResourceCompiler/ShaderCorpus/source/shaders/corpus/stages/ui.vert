#version 410 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

out vec2 vTexCoord;
out vec4 vColor;

void main()
{
    vTexCoord = aTexCoord;
    vColor = aColor;
#if defined(CY_UI_PIXEL_SNAP)
    vec2 snapped = floor(aPosition + vec2(0.5));
    gl_Position = vec4(snapped, 0.0, 1.0);
#else
    gl_Position = vec4(aPosition, 0.0, 1.0);
#endif
}
