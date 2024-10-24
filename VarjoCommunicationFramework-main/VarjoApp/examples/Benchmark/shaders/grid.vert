#version 430

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(push_constant) uniform Matrices {
    mat4 viewMatrix;
    mat4 projectionMatrix;
};

layout(location = 0) out vec3 vPosition;

void main() {
    vPosition = position + 0.5;

    mat4 view = viewMatrix;
    view[3][0] = 0;
    view[3][1] = 0;
    view[3][2] = 0;
    gl_Position = projectionMatrix * view * vec4(position, 1);
}
