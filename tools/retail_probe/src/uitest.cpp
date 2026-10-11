// Overlay check without the game: a bare Direct3D 9 window that loads dinput8.dll
// the way the game does (LoadLibrary + DirectInput8Create + a keyboard device),
// renders frames so the DLL's EndScene hook draws the panel, replays mouse clicks,
// and saves the back buffer as a BMP.
//
//   probe_uitest <dinput8.dll> <out.bmp> [x,y@frame ...] [key:<vk>@frame ...]
//
// The exe here is not yso_win.exe, so the DLL reports "unsupported build" and
// stays read-only: this shows the loader, the hooks and the panel, not the game side.
#include <windows.h>
#include <d3d9.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct Ev { int frame, x, y, vk; };

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: probe_uitest <dinput8.dll> <out.bmp> [x,y@frame ...] [key:vk@frame ...]\n"); return 2; }
    std::vector<Ev> evs;
    for (int i = 3; i < argc; i++) {
        Ev e{0, -1, -1, 0};
        if (sscanf(argv[i], "key:%i@%d", &e.vk, &e.frame) == 2 || sscanf(argv[i], "%d,%d@%d", &e.x, &e.y, &e.frame) == 3) evs.push_back(e);
    }
    const int W = 1280, H = 720, FRAMES = 150;
    WNDCLASSEXA wc{sizeof(wc)};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "probe_uitest";
    RegisterClassExA(&wc);
    RECT r{0, 0, W, H};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND wnd = CreateWindowA(wc.lpszClassName, "probe_uitest", WS_OVERLAPPEDWINDOW, 40, 40, r.right - r.left, r.bottom - r.top,
                             nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(wnd, SW_SHOWNOACTIVATE);

    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) { printf("FAIL LoadLibrary error %lu\n", GetLastError()); return 1; }
    // The game's own first call into dinput8: create the interface, then a device.
    typedef HRESULT(WINAPI* DI8Create)(HINSTANCE, DWORD, const void*, void**, void*);
    DI8Create create = (DI8Create)GetProcAddress(dll, "DirectInput8Create");
    static const GUID iid_di8a = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
    static const GUID guid_kbd = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
    void* di = nullptr;
    HRESULT hr = create ? create(wc.hInstance, 0x0800, &iid_di8a, &di, nullptr) : E_FAIL;
    printf("DirectInput8Create through the proxy: 0x%08lX\n", hr);
    void* kbd = nullptr;
    if (SUCCEEDED(hr) && di) {
        typedef HRESULT(WINAPI* CreateDevice)(void*, const GUID*, void**, void*);
        hr = ((CreateDevice)(*(void***)di)[3])(di, &guid_kbd, &kbd, nullptr);
        printf("CreateDevice(keyboard): 0x%08lX\n", hr);
    }

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = wnd;
    pp.BackBufferWidth = W;
    pp.BackBufferHeight = H;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    IDirect3DDevice9* dev = nullptr;
    hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr) || !dev) { printf("FAIL CreateDevice 0x%08lX\n", hr); return 1; }
    Sleep(1500);   // the DLL hooks EndScene from its own thread

    for (int f = 0; f < FRAMES; f++) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        for (const Ev& e : evs) {
            if (e.vk) {
                if (e.frame == f) SendMessageA(wnd, WM_KEYDOWN, (WPARAM)e.vk, 1);
                if (e.frame + 1 == f) SendMessageA(wnd, WM_KEYUP, (WPARAM)e.vk, 0xC0000001);
                continue;
            }
            LPARAM lp = MAKELPARAM(e.x, e.y);
            if (e.frame - 2 == f) SendMessageA(wnd, WM_MOUSEMOVE, 0, lp);
            if (e.frame == f) SendMessageA(wnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
            if (e.frame + 2 == f) SendMessageA(wnd, WM_LBUTTONUP, 0, lp);
        }
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(40, 70, 60), 1.0f, 0);
        dev->BeginScene();
        dev->EndScene();
        if (f == FRAMES - 1) {
            IDirect3DSurface9 *bb = nullptr, *sys = nullptr;
            dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
            dev->CreateOffscreenPlainSurface(W, H, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr);
            D3DLOCKED_RECT lr{};
            if (bb && sys && SUCCEEDED(dev->GetRenderTargetData(bb, sys)) && SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                FILE* out = fopen(argv[2], "wb");
                BITMAPFILEHEADER fh{};
                BITMAPINFOHEADER ih{};
                fh.bfType = 0x4D42;
                fh.bfOffBits = sizeof(fh) + sizeof(ih);
                fh.bfSize = fh.bfOffBits + W * H * 4;
                ih.biSize = sizeof(ih);
                ih.biWidth = W;
                ih.biHeight = -H;
                ih.biPlanes = 1;
                ih.biBitCount = 32;
                fwrite(&fh, sizeof(fh), 1, out);
                fwrite(&ih, sizeof(ih), 1, out);
                for (int y = 0; y < H; y++) fwrite((unsigned char*)lr.pBits + y * lr.Pitch, 4, W, out);
                fclose(out);
                sys->UnlockRect();
                printf("saved %s\n", argv[2]);
            } else printf("FAIL could not read the back buffer\n");
        }
        dev->Present(nullptr, nullptr, nullptr, nullptr);
        Sleep(10);
    }
    if (kbd) {   // one GetDeviceState through the hook
        unsigned char keys[256];
        typedef HRESULT(WINAPI* GetState)(void*, DWORD, void*);
        hr = ((GetState)(*(void***)kbd)[9])(kbd, 256, keys);
        printf("GetDeviceState through the hook: 0x%08lX (an error code is fine: the device was never acquired)\n", hr);
    }
    printf("done\n");
    return 0;
}
