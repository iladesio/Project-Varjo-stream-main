#pragma once

#include <d3d11.h>

class RendererSettings;

namespace D3DShaders
{
ID3DBlob* compileGridVertexShader();
ID3DBlob* compileGridPixelShader();
ID3DBlob* compileDefaultVertexShader(const RendererSettings& settings);
ID3DBlob* compileDefaultPixelShader(const RendererSettings& settings);
ID3DBlob* compileOcclusionVertexShader();
ID3DBlob* compileOcclusionPixelShader();
ID3DBlob* compileVrsVisualizeShader();
}  // namespace D3DShaders