#version 410 core
layout(location = 0) in vec3 position;
layout(std140) uniform Light { mat4 lightClipFromWorld; };
layout(std140) uniform Model { mat4 model; };
void main() {
    gl_Position = lightClipFromWorld * model * vec4(position, 1.0);
}
