layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in mat4 worldMat;
layout(location = 6) in mat4 nextWorldMat;

layout(push_constant) uniform Matrices {
    mat4 viewMatrix;
    mat4 projectionMatrix;
    vec2 viewportSize;
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vVelocity;

void main() {
    vec4 pos = projectionMatrix * viewMatrix * worldMat * vec4(position, 1);

    vNormal = (worldMat * vec4(normal, 0)).xyz;
    gl_Position = pos;

#ifdef USE_VELOCITY
    vec4 nextPos = projectionMatrix * viewMatrix * nextWorldMat * vec4(position, 1);

    vVelocity = ((nextPos.xy / nextPos.w) - (pos.xy / pos.w)) * vec2(0.5f, -0.5f) * viewportSize;
#endif
}
