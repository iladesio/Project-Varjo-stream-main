#include "D3DShaders.hpp"
#include "IRenderer.hpp"

#include <cstdio>
#include <d3dcompiler.h>
#include <string>

namespace D3DShaders
{
namespace
{
std::string generateShaderHeader(const RendererSettings& settings)
{
    std::string shaderHeader;
    if (settings.useVelocity()) {
        shaderHeader.append("#define USE_VELOCITY\n");
        shaderHeader.append("#define PRECISION " + std::to_string(IRenderer::c_velocityPrecision) + "\n");
    }
    return shaderHeader;
}
}  // namespace

#define HCHECK(value) HCheck(#value, __LINE__, value)

void HCheck(const char* what, int line, HRESULT hr)
{
    if (FAILED(hr)) {
        printf("%s failed with code %x at line %d\n", what, hr, line);
        abort();
    }
}

ID3DBlob* compileShader(const char* src, const char* target, const char* name)
{
    ID3DBlob* compiledShader = nullptr;
    ID3DBlob* compilerMsgs = nullptr;
#ifdef _DEBUG
    const UINT flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_IEEE_STRICTNESS;
#else
    const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    HRESULT hr = D3DCompile(src, strlen(src) + 1, name, nullptr, nullptr, "main", target, flags, 0, &compiledShader, &compilerMsgs);

    if (compilerMsgs != nullptr) {
        printf("compiler errors: '%.*s'\n", static_cast<int>(compilerMsgs->GetBufferSize()), reinterpret_cast<char*>(compilerMsgs->GetBufferPointer()));
        compilerMsgs->Release();
    }

    HCHECK(hr);
    return compiledShader;
}


ID3DBlob* compileGridVertexShader()
{
    return compileShader(
        "cbuffer ConstantBuffer : register(b0) {\n"
        "  matrix view;\n"
        "  matrix projection;\n"
        "};\n"
        "struct VsInput {\n"
        "  float3 pos : POSITION;\n"
        "  float3 normal : NORMAL;\n"
        "};\n"
        "struct VsOutput {\n"
        "  float4 position : SV_POSITION;\n"
        "  float3 vPosition: TEXCOORD0;\n"
        "};\n"
        "VsOutput main(VsInput input) {\n"
        "  VsOutput output;\n"
        "\n"
        "  matrix viewMatrix = view;\n"
        "  viewMatrix[3][0] = 0.0;\n"
        "  viewMatrix[3][1] = 0.0;\n"
        "  viewMatrix[3][2] = 0.0;\n"
        "\n"
        "  float4 pos = float4(input.pos, 1.0f);\n"
        "  pos = mul(pos, viewMatrix);\n"
        "  pos = mul(pos, projection);\n"
        "\n"
        "  output.position = pos;\n"
        "  output.vPosition = input.pos.xyz + 0.5;\n"
        "  return output;\n"
        "}\n",
        "vs_4_0", "vertex");
}

ID3DBlob* compileGridPixelShader()
{
    return compileShader(
        "float grid(float coordinate, float size) {\n"
        "    float inRange = step(0.001, 1.0 - coordinate) * step(0.001, coordinate);\n"
        "    float x = (coordinate * size);\n"
        "    return inRange * step(0.25, x - floor(x)) * step(0.25, 1.0 - (x - floor(x)));\n"
        "}\n"
        "\n"
        "struct PsInput {\n"
        "  float4 position : SV_POSITION;\n"
        "  float3 vPosition : TEXCOORD0;\n"
        "};\n"
        "\n"
        "float4 main(PsInput input) : SV_TARGET {\n"
        "  float x = grid(input.vPosition.x, 4.0);\n"
        "  float y = grid(input.vPosition.y, 4.0);\n"
        "  float z = grid(input.vPosition.z, 4.0);\n"
        "  float value = saturate(x + y + z);\n"
        "\n"
        "  float3 color = step(0.999, input.vPosition.z) * (float3)lerp(0.65, 0.427, value);\n"
        "  color += step(0.999, 1.0 - input.vPosition.z) * (float3)lerp(1.0, 0.0, value);\n"
        "\n"
        "  color += step(0.999, input.vPosition.x) * lerp(float3(1, 0, 0), float3(0, 1, 0), value);\n"
        "  color += step(0.999, 1.0 - input.vPosition.x) * lerp(float3(1, 0, 1), float3(0, 0, 1), value);\n"
        "\n"
        "  color += step(0.999, input.vPosition.y) * lerp(float3(1, 1, 0), float3(0, 1, 1), value);\n"
        "  color += step(0.999, 1.0 - input.vPosition.y) * lerp(float3(0.25, 0, 0.392), float3(0, 0.392, 0.129), value);\n"
        "\n"
        "  x = grid(input.vPosition.x, 16.0);\n"
        "  y = grid(input.vPosition.y, 16.0);\n"
        "  z = grid(input.vPosition.z, 16.0);\n"
        "  value = saturate(x + y + z);\n"
        "  float alpha = lerp(1, 0, value);\n"
        "\n"
        "  return float4(color*alpha, alpha);\n"
        "}\n",
        "ps_4_0", "pixel");
}

ID3DBlob* compileDefaultVertexShader(const RendererSettings& settings)
{
    const std::string shaderHeader = generateShaderHeader(settings);

    constexpr char* shaderCode = R"hlsl(
        cbuffer ConstantBuffer : register(b0) {
          matrix view;
          matrix projection;
          float2 viewportSize;
        };
        struct VsInput {
          float3 pos : POSITION;
          float3 normal : NORMAL;
          float4 world0 : TEXCOORD0;
          float4 world1 : TEXCOORD1;
          float4 world2 : TEXCOORD2;
          float4 world3 : TEXCOORD3;
          float4 nextWorld0 : TEXCOORD4;
          float4 nextWorld1 : TEXCOORD5;
          float4 nextWorld2 : TEXCOORD6;
          float4 nextWorld3 : TEXCOORD7;
        };
        struct VsOutput {
          float4 position : SV_POSITION;
          float3 normal : TEXCOORD0;
        #ifdef USE_VELOCITY
          float2 velocity : TEXCOORD1;
        #endif
        };
        VsOutput main(VsInput input) {
          VsOutput output;

          matrix world = matrix(input.world0, input.world1, input.world2, input.world3);

          float4 pos = float4(input.pos, 1.0f);
          pos = mul(pos, world);
          pos = mul(pos, view);
          pos = mul(pos, projection);

          output.position = pos;
          output.normal = mul(float4(input.normal, 0.0f), world).xyz;

        #ifdef USE_VELOCITY
          matrix nextWorld = matrix(input.nextWorld0, input.nextWorld1, input.nextWorld2, input.nextWorld3);
          float4 nextPos = mul(mul(mul(float4(input.pos, 1.0f), nextWorld), view), projection);
          output.velocity = ((nextPos.xy / nextPos.w) - (pos.xy / pos.w)) * float2(0.5f, -0.5f) * viewportSize;
        #endif
          return output;
        }
    )hlsl";

    return compileShader((shaderHeader + shaderCode).c_str(), "vs_4_0", "vertex");
}

ID3DBlob* compileDefaultPixelShader(const RendererSettings& settings)
{
    const std::string shaderHeader = generateShaderHeader(settings);

    constexpr char* shaderCode = R"hlsl(
        struct PsInput {
            float4 position : SV_POSITION;
            float3 normal : TEXCOORD0;
        #ifdef USE_VELOCITY
            float2 velocity : TEXCOORD1;
        #endif
        };
        #ifdef USE_VELOCITY

        struct PsOutput {
            float4 color: SV_Target0;
            uint4 velocity: SV_Target1;
        };
        uint4 packVelocity(float2 floatingPoint)
        {
            int2 fixedPoint = floatingPoint * PRECISION;
            uint2 temp = uint2(fixedPoint.x & 0xFFFF, fixedPoint.y & 0xFFFF);
            return uint4(temp.r >> 8, temp.r & 0xFF, temp.g >> 8, temp.g & 0xFF);
        }
        PsOutput main(PsInput input) {
            PsOutput output;
            output.color = float4(input.normal, 1);
            output.velocity = packVelocity(input.velocity);
            return output;
        }

        #else

        float4 main(PsInput input) : SV_TARGET {
            return float4(input.normal, 1);
        }
        #endif
    )hlsl";

    return compileShader((shaderHeader + shaderCode).c_str(), "ps_4_0", "pixel");
}

ID3DBlob* compileOcclusionVertexShader()
{
    return compileShader(
        "struct VsInput {"
        "  float2 pos : POSITION;"
        "};"
        "struct VsOutput {"
        "  float4 position : SV_POSITION;"
        "};"
        "VsOutput main(VsInput input) {"
        "  VsOutput output;"
        "  output.position = float4(input, 0.0f, 1.0f);"
        "  return output;"
        "}",
        "vs_4_0", "vertex");
}

ID3DBlob* compileOcclusionPixelShader()
{
    return compileShader(
        "struct PsInput {"
        "  float4 position : SV_POSITION;"
        "};"
        "float4 main(PsInput input) : SV_TARGET {"
        "  return float4(0.0f, 0.0f, 0.0f, 1.0f);"
        "}",
        "ps_4_0", "pixel");
}

ID3DBlob* compileVrsVisualizeShader()
{
    return compileShader(
        R"shader(
        static float4 vrsColors[11] = {
            float4(0, 0, 1, 0.2f),             // 0
            float4(1, 1, 0, 0.5f),             // 1
            float4(0,0,0,0),
            float4(0,0,0,0),
            float4(0, 0, 1, 0.2f),             // 4
            float4(0, 1, 0, 0.5f),             // 5
            float4(0.54f, 0.19f, 0.88f, 0.5f), // 6
            float4(0, 1, 0, 0.5f),
            float4(0,0,0,0),
            float4(0.54f, 0.19f, 0.88f, 0.5f), // 9
            float4(1, 0, 0, 0.5f),             // 10
        };

        cbuffer Constants : register(b0) {
            float2 texSize;
            float2 vrsMapSize;
        };

        RWTexture2D<unorm float4> tex : register(u0);
        RWTexture2D<uint> vrsMap: register(u1);

        [numthreads(8, 8, 1)]
        void main(uint3 id: SV_DispatchThreadID) {
            float2 uv = id.xy / texSize;
            uint2 vrsMapPos = uv * vrsMapSize;
            uint vrsValue = vrsMap[vrsMapPos];
            float4 pixel = tex[id.xy];
            float4 vrsColor = vrsColors[vrsValue];
            tex[id.xy] = pixel * vrsColor;
        }
)shader",
        "cs_5_0", "compute");
}

}  // namespace D3DShaders
