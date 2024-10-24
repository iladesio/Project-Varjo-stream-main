#include <Windows.h>
#include "Window.hpp"
#include <dxgi.h>
#include <wrl.h>
#include <cstdio>

static LRESULT CALLBACK WinProc(HWND handle, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_DESTROY || msg == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(handle, msg, wparam, lparam);
}

Window::Window(int width, int height, bool invisible)
    : m_width(width)
    , m_height(height)
{
    // Define window style
    WNDCLASS wc = {0};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WinProc;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"Benchmark";
    RegisterClass(&wc);
    // Create the window
    m_handle = CreateWindow(
        L"Benchmark", L"Benchmark", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 100, 100, width, height, nullptr, nullptr, nullptr, nullptr);

    if (invisible) {
        ShowWindow(m_handle, 0);
    }
}

Window::~Window() { CloseWindow(m_handle); }


void Window::present(HDC hdc) const  //
{
    SwapBuffers(hdc);
}

void Window::present(IDXGISwapChain1* swapChain) const
{
    DXGI_PRESENT_PARAMETERS parameters{};
    swapChain->Present1(0, 0, &parameters);
}

bool Window::runEventLoop() const
{
    MSG msg = {nullptr};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT) {
            return false;
        }
    }

    return true;
}
