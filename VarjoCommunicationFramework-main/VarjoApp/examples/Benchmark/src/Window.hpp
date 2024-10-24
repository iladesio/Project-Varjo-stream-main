#pragma once
#include <Windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <wrl.h>

class Window
{
public:
    Window(int width, int height, bool invisible);
    ~Window();

    HWND getHandle() const { return m_handle; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    bool runEventLoop() const;
    void present(HDC hdc) const;
    void present(IDXGISwapChain1* swapChain) const;

private:
    HWND m_handle;
    int m_width{};
    int m_height{};
};
