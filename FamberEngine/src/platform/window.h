// Win32 window + WGL context + input + screenshot. Header-only (used once).
#pragma once
#include "gl.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace plat {

static HWND  hwnd = nullptr;
static HDC   hdc = nullptr;
static HGLRC hglrc = nullptr;
static int   W = 1280, H = 720;
static bool  run = true, act = false, cursorHidden = false;
static POINT center{};

inline LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CLOSE: case WM_DESTROY: run = false; return 0;
        case WM_SIZE: W = LOWORD(lp); H = HIWORD(lp); return 0;
        case WM_ACTIVATEAPP: act = (wp != FALSE); return 0;
        case WM_KEYDOWN: if (wp == VK_ESCAPE) run = false; return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

inline bool init(int w, int h, const char* title) {
    W = w; H = h;
    HINSTANCE inst = GetModuleHandle(nullptr);
    WNDCLASSA wc = {};
    wc.style = CS_OWNDC; wc.lpfnWndProc = WndProc; wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = "FamberEngine";
    RegisterClassA(&wc);

    RECT r = {0, 0, W, H};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowA("FamberEngine", title, WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                         nullptr, nullptr, inst, nullptr);
    if (!hwnd) { printf("CreateWindow failed\n"); return false; }
    hdc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24;
    SetPixelFormat(hdc, ChoosePixelFormat(hdc, &pfd), &pfd);

    hglrc = wglCreateContext(hdc);
    wglMakeCurrent(hdc, hglrc);
    return true;
}

inline void show() { ShowWindow(hwnd, SW_SHOW); }
inline bool running() { return run; }
inline bool active() { return act; }
inline void size(int& w, int& h) { w = W; h = H; }
inline void swap() { SwapBuffers(hdc); }
inline bool key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

inline void poll() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
}

inline void recenter() {
    RECT rc; GetClientRect(hwnd, &rc);
    POINT c = {(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
    ClientToScreen(hwnd, &c);
    center = c; SetCursorPos(c.x, c.y);
}

// Relative mouse look; hides & recenters cursor while the window is active.
inline void mouseLook(float sens, float& dyaw, float& dpitch) {
    dyaw = dpitch = 0;
    if (!act) { if (cursorHidden) { ShowCursor(TRUE); cursorHidden = false; } return; }
    if (!cursorHidden) { ShowCursor(FALSE); recenter(); cursorHidden = true; }
    POINT p; GetCursorPos(&p);
    dyaw = -(float)(p.x - center.x) * sens;
    dpitch = (float)(p.y - center.y) * sens;
    SetCursorPos(center.x, center.y);
}

inline void shutdown() {
    wglMakeCurrent(nullptr, nullptr);
    if (hglrc) wglDeleteContext(hglrc);
}

inline void saveBMP(const char* path) {
    std::vector<unsigned char> px(W * H * 3);
    glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    int rowSize = (W * 3 + 3) & ~3;
    int fileSize = 54 + rowSize * H;
    FILE* f = fopen(path, "wb");
    if (!f) { printf("[shot] cannot open %s\n", path); return; }
    unsigned char fh[14] = {'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0};
    fh[2] = fileSize & 0xFF; fh[3] = (fileSize >> 8) & 0xFF;
    fh[4] = (fileSize >> 16) & 0xFF; fh[5] = (fileSize >> 24) & 0xFF;
    fwrite(fh, 1, 14, f);
    unsigned char ih[40] = {0}; ih[0] = 40;
    ih[4] = W & 0xFF; ih[5] = (W >> 8) & 0xFF; ih[6] = (W >> 16) & 0xFF; ih[7] = (W >> 24) & 0xFF;
    ih[8] = H & 0xFF; ih[9] = (H >> 8) & 0xFF; ih[10] = (H >> 16) & 0xFF; ih[11] = (H >> 24) & 0xFF;
    ih[12] = 1; ih[14] = 24;
    fwrite(ih, 1, 40, f);
    std::vector<unsigned char> row(rowSize, 0);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            row[x * 3 + 0] = px[(y * W + x) * 3 + 2];
            row[x * 3 + 1] = px[(y * W + x) * 3 + 1];
            row[x * 3 + 2] = px[(y * W + x) * 3 + 0];
        }
        fwrite(row.data(), 1, rowSize, f);
    }
    fclose(f);
    printf("[shot] wrote %s (%dx%d)\n", path, W, H);
}

} // namespace plat
