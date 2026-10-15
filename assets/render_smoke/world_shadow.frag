#version 410 core
// Opaque depth by default; the MASK recipe samples the same raw albedo alpha
// and cutoff as the camera pass so holes also exist in the shadow map.
#ifdef CY_WORLD_CUTOUT
uniform sampler2D albedoMap;
in vec2 surfaceUV;
flat in float cutoutThreshold;
#endif
void main() {
#ifdef CY_WORLD_CUTOUT
    if (texture(albedoMap, surfaceUV).a < cutoutThreshold) discard;
#endif
}
