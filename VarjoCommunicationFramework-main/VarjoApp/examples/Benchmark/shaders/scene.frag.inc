layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vVelocity;

layout(location = 0) out vec4 oColor;

#ifdef USE_VELOCITY
layout(location = 1) out uvec4 oVelocity;

uvec4 packVelocity(vec2 floatingPoint)
{
    ivec2 fixedPoint = ivec2(floatingPoint * PRECISION);
    uvec2 temp = uvec2(fixedPoint.x & 0xFFFF, fixedPoint.y & 0xFFFF);
    return uvec4(temp.r >> 8, temp.r & 0xFF, temp.g >> 8, temp.g & 0xFF);
}
#endif

void main() {
    oColor = vec4(vNormal, 1);

#ifdef USE_VELOCITY
    oVelocity = packVelocity(vVelocity);
#endif
}
