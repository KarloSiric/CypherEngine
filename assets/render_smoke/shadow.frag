#version 410 core

in vec2 faceUV;
layout(std140) uniform ObjectConstants {
    mat4 model;
    vec4 tint;
    vec4 cutout;
};
uniform sampler2D albedoTexture;

// The target has only a D24 attachment. Discarding here must match the camera
// pass so a masked hole leaves the shadow map's previous depth untouched.
void main()
{
    if (texture(albedoTexture, faceUV).a < cutout.x) discard;
}
