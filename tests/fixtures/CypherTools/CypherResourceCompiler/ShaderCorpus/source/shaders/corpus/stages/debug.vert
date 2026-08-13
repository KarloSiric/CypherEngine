#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

out vec4 vColor;
out float vLinearDepth;

void main()
{
    vec4 clipPosition = vec4(aPosition, 1.0);
    vColor = aColor;
    vLinearDepth = abs(clipPosition.w);
    gl_Position = clipPosition;
#if defined(CY_DEBUG_POINTS)
    gl_PointSize = 3.0;
#endif
}
