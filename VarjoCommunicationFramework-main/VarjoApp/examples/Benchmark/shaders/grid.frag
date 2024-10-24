#version 430

layout(location = 0) in vec3 vPosition;

layout(location = 0) out vec4 oColor;

float grid(float coordinate, float size) {
    float inRange = step(0.001, 1.0 - coordinate) * step(0.001, coordinate);
    float x = (coordinate * size);
    return inRange * step(0.25, x - floor(x)) * step(0.25, 1.0 - (x - floor(x)));
}

void main() {
    float x = grid(vPosition.x, 4.0f);
    float y = grid(vPosition.y, 4.0f);
    float z = grid(vPosition.z, 4.0f);
    float value = clamp(x + y + z, 0.0, 1.0);

    vec3 color = vec3(step(0.999, vPosition.z) * mix(0.65, 0.427, value));
    color += step(0.999, 1.0 - vPosition.z) * mix(1.0, 0.0, value);

    color += step(0.999, vPosition.x) * mix(vec3(1, 0, 0), vec3(0, 1, 0), value);
    color += step(0.999, 1.0 - vPosition.x) * mix(vec3(1, 0, 1), vec3(0, 0, 1), value);

    color += step(0.999, vPosition.y) * mix(vec3(1, 1, 0), vec3(0, 1, 1), value);
    color += step(0.999, 1.0 - vPosition.y) * mix(vec3(0.25, 0, 0.392), vec3(0, 0.392, 0.129), value);

    x = grid(vPosition.x, 16.0);
    y = grid(vPosition.y, 16.0);
    z = grid(vPosition.z, 16.0);
    value = clamp(x + y + z, 0.0, 1.0);
    float alpha = mix(1, 0, value);
    oColor = vec4(color*alpha, alpha);
}
