// Loader side: the dinput8 proxy, the log, the Direct3D 9 EndScene/Reset hooks that
// draw the Dear ImGui panel, the window subclass for input, and the DirectInput
// keyboard freeze while a text box of the panel has the focus.
//
// Same mechanism as the user's Archipelago mod (YsOrigin-Archipelago/mod/src:
// proxy_dinput8.cpp, hook_d3d9.cpp, hook_input.cpp): yso_win.exe imports
// DirectInput8Create from dinput8.dll, so a dinput8.dll next to the exe is loaded
// in its place and forwards to the real one.
#include <windows.h>
#include <d3d9.h>
#include <objbase.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "probe.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace panel {
void draw();
void toggle();
bool is_open();
bool wants_text();
int toggle_vk();
void init();
}

// ---- module folder + log ------------------------------------------------------------
static HMODULE g_self = nullptr;
static char g_dir[MAX_PATH] = "";
const char* module_dir() {
    if (!g_dir[0]) {
        GetModuleFileNameA(g_self, g_dir, MAX_PATH);
        char* slash = strrchr(g_dir, '\\');
        if (slash) slash[1] = 0;
    }
    return g_dir;
}
static FILE* g_logf = nullptr;
static std::mutex g_log_mtx;
void plog(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_log_mtx);
    if (!g_logf) {
        char path[MAX_PATH], prev[MAX_PATH];
        snprintf(path, sizeof(path), "%sretail_probe.log", module_dir());
        snprintf(prev, sizeof(prev), "%sretail_probe.prev.log", module_dir());
        DeleteFileA(prev);
        MoveFileA(path, prev);
        g_logf = fopen(path, "w");
        if (!g_logf) return;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_logf, "%02d:%02d:%02d.%03d  ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_logf, fmt, ap);
    va_end(ap);
    fputc('\n', g_logf);
    fflush(g_logf);
}

// ---- MinHook init, reachable from two threads ---------------------------------------
static volatile LONG g_mh_state = 0;
void mh_ensure_init() {
    if (InterlockedCompareExchange(&g_mh_state, 1, 0) == 0) {
        MH_Initialize();
        InterlockedExchange(&g_mh_state, 2);
    } else {
        while (InterlockedCompareExchange(&g_mh_state, 2, 2) != 2) Sleep(0);
    }
}

// ---- dinput8 proxy -------------------------------------------------------------------
// The real DLL is the system one, or - only when the user installed with -KeepAP -
// the Archipelago mod kept under the name dinput8_chain.dll, which then forwards
// to the system one itself.
static HMODULE g_real = nullptr;
static void proxy_load_real() {
    if (g_real) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sdinput8_chain.dll", module_dir());
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        g_real = LoadLibraryA(path);
        plog("proxy: chained to dinput8_chain.dll -> %p", (void*)g_real);
    }
    if (!g_real) {
        UINT n = GetSystemDirectoryA(path, MAX_PATH);
        lstrcpyA(path + n, "\\dinput8.dll");
        g_real = LoadLibraryA(path);
        plog("proxy: system dinput8 -> %p", (void*)g_real);
    }
}
static FARPROC real(const char* name) {
    if (!g_real) proxy_load_real();
    return g_real ? GetProcAddress(g_real, name) : nullptr;
}

// Keyboard freeze: IDirectInputDevice8::GetDeviceState (vtable slot 9), reached
// through IDirectInput8::CreateDevice (slot 3). The keyboard state is the 256-byte
// buffer; joystick buffers are left alone.
using GetDeviceState_t = HRESULT(WINAPI*)(void*, DWORD, void*);
using CreateDevice_t = HRESULT(WINAPI*)(void*, const void*, void**, void*);
static GetDeviceState_t o_GetDeviceState = nullptr;
static CreateDevice_t o_CreateDevice = nullptr;
static bool g_dev_hooked = false;
static HRESULT WINAPI hk_GetDeviceState(void* self, DWORD cb, void* data) {
    HRESULT hr = o_GetDeviceState(self, cb, data);
    if (data && cb == 256 && panel::wants_text()) memset(data, 0, cb);
    return hr;
}
static HRESULT WINAPI hk_CreateDevice(void* self, const void* guid, void** dev, void* outer) {
    HRESULT hr = o_CreateDevice(self, guid, dev, outer);
    if (SUCCEEDED(hr) && dev && *dev && !g_dev_hooked) {
        void** vt = *reinterpret_cast<void***>(*dev);
        if (MH_CreateHook(vt[9], (void*)&hk_GetDeviceState, (void**)&o_GetDeviceState) == MH_OK &&
            MH_EnableHook(vt[9]) == MH_OK) {
            g_dev_hooked = true;
            plog("input: GetDeviceState hooked");
        } else {
            plog("input: GetDeviceState hook FAILED (typing in the panel will also move the character)");
        }
    }
    return hr;
}

extern "C" {
HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD ver, const void* riid, void** out, void* outer) {
    typedef HRESULT(WINAPI* fn)(HINSTANCE, DWORD, const void*, void**, void*);
    static fn p = (fn)real("DirectInput8Create");
    if (!p) return E_FAIL;
    HRESULT hr = p(hinst, ver, riid, out, outer);
    if (SUCCEEDED(hr) && out && *out) {
        mh_ensure_init();
        void** vt = *reinterpret_cast<void***>(*out);
        if (!o_CreateDevice && MH_CreateHook(vt[3], (void*)&hk_CreateDevice, (void**)&o_CreateDevice) == MH_OK &&
            MH_EnableHook(vt[3]) == MH_OK)
            plog("input: CreateDevice hooked");
    }
    return hr;
}
HRESULT WINAPI DllCanUnloadNow() {
    typedef HRESULT(WINAPI* fn)();
    static fn p = (fn)real("DllCanUnloadNow");
    return p ? p() : S_FALSE;
}
HRESULT WINAPI DllGetClassObject(REFCLSID a, REFIID b, LPVOID* c) {
    typedef HRESULT(WINAPI* fn)(REFCLSID, REFIID, LPVOID*);
    static fn p = (fn)real("DllGetClassObject");
    return p ? p(a, b, c) : E_FAIL;
}
HRESULT WINAPI DllRegisterServer() {
    typedef HRESULT(WINAPI* fn)();
    static fn p = (fn)real("DllRegisterServer");
    return p ? p() : E_FAIL;
}
HRESULT WINAPI DllUnregisterServer() {
    typedef HRESULT(WINAPI* fn)();
    static fn p = (fn)real("DllUnregisterServer");
    return p ? p() : E_FAIL;
}
void* WINAPI GetdfDIJoystick() {
    typedef void*(WINAPI* fn)();
    static fn p = (fn)real("GetdfDIJoystick");
    return p ? p() : nullptr;
}
}  // extern "C"

// ---- Direct3D 9 ------------------------------------------------------------------------
using EndScene_t = HRESULT(WINAPI*)(IDirect3DDevice9*);
using Reset_t = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static EndScene_t o_EndScene = nullptr;
static Reset_t o_Reset = nullptr;
static WNDPROC o_WndProc = nullptr;
static HWND g_hwnd = nullptr;
static bool g_imgui_ready = false;

static LRESULT CALLBACK hk_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && (int)wp == panel::toggle_vk() && !(lp & (1 << 30))) {
        panel::toggle();
        return 0;
    }
    if (panel::is_open()) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
        ImGuiIO& io = ImGui::GetIO();
        bool mouse = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST);
        bool key = (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR);
        if ((mouse && io.WantCaptureMouse) || (key && io.WantTextInput)) return 0;
    }
    return CallWindowProc(o_WndProc, hwnd, msg, wp, lp);
}

static void imgui_init(IDirect3DDevice9* dev) {
    D3DDEVICE_CREATION_PARAMETERS cp{};
    dev->GetCreationParameters(&cp);
    g_hwnd = cp.hFocusWindow;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFont* f = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    if (!f) io.Fonts->AddFontDefault();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX9_Init(dev);
    o_WndProc = (WNDPROC)SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)hk_WndProc);
    panel::init();
    g_imgui_ready = true;
    plog("overlay: ready (window %p)", (void*)g_hwnd);
}

static HRESULT WINAPI hk_EndScene(IDirect3DDevice9* dev) {
    if (!g_imgui_ready) imgui_init(dev);
    probe::frame();                     // main thread: the only place game state is written
    // Draw only into the back buffer: if the game ends a scene on an off-screen
    // target too (a reflection, a shadow map), the panel must not end up in it.
    // If the back buffer is never the target at EndScene, draw every time instead.
    static int since_bb = 1000;
    bool is_bb = false;
    IDirect3DSurface9 *rt = nullptr, *bb = nullptr;
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) is_bb = (rt == bb);
    if (rt) rt->Release();
    if (bb) bb->Release();
    if (is_bb) since_bb = 0;
    else if (since_bb < 1000) since_bb++;
    if (panel::is_open() && (is_bb || since_bb >= 240)) {
        ImGui::GetIO().MouseDrawCursor = true;
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        panel::draw();
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }
    return o_EndScene(dev);
}
static HRESULT WINAPI hk_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    if (g_imgui_ready) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = o_Reset(dev, pp);
    if (g_imgui_ready) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

// Read the device vtable from a throwaway device (EndScene = slot 42, Reset = 16).
static bool get_device_vtable(void** vtable, size_t count) {
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return false;
    WNDCLASSEXA wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "retail_probe_tmp";
    RegisterClassExA(&wc);
    HWND wnd = CreateWindowA(wc.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = wnd;
    pp.BackBufferWidth = 1;
    pp.BackBufferHeight = 1;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferCount = 1;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr) || !dev)
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    bool ok = false;
    if (SUCCEEDED(hr) && dev) {
        memcpy(vtable, *reinterpret_cast<void***>(dev), count * sizeof(void*));
        dev->Release();
        ok = true;
    }
    d3d->Release();
    DestroyWindow(wnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    plog("d3d9: vtable probe hr=0x%08lX ok=%d", hr, (int)ok);
    return ok;
}

static DWORD WINAPI init_thread(LPVOID) {
    void* vt[119] = {};
    if (!get_device_vtable(vt, 119)) {
        plog("init: no Direct3D 9 vtable - the mod stays inactive");
        return 0;
    }
    mh_ensure_init();
    MH_STATUS a = MH_CreateHook(vt[42], (void*)&hk_EndScene, (void**)&o_EndScene);
    MH_STATUS b = MH_CreateHook(vt[16], (void*)&hk_Reset, (void**)&o_Reset);
    if (a == MH_OK) a = MH_EnableHook(vt[42]);
    if (b == MH_OK) b = MH_EnableHook(vt[16]);
    plog("init: EndScene hook=%d Reset hook=%d (0 = ok)", (int)a, (int)b);
    probe::install();
    return 0;
}

// True only inside probe_simtest.exe (the offline simulation, src/simtest.cpp).
bool host_is_simtest() {
    char exe[MAX_PATH] = "";
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    const char* base = strrchr(exe, '\\');
    return _stricmp(base ? base + 1 : exe, "probe_simtest.exe") == 0;
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = mod;
        DisableThreadLibraryCalls(mod);
        plog("retail_probe loaded (%s)", module_dir());
        if (host_is_simtest()) {
            plog("simulation host: no Direct3D / input hooks; the game hooks wait for the host");
            return TRUE;
        }
        proxy_load_real();
        CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);
    }
    return TRUE;
}
