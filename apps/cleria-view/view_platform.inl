// cleria-view — the per-platform window glue (issue #46): the WebGPU surface for the SDL window, and the
// small OS calls the shell makes (this exe's path, opening a folder, a message box).
// Textual part of main.cpp: #included before view_game.inl; ONE translation unit, not standalone.
//
// Windows: an HWND surface (what render_window always used). Linux: X11 or Wayland, whichever SDL's
// video driver is. macOS: a CAMetalLayer from SDL_Metal_CreateView (the window is created with
// SDL_WINDOW_METAL, kWindowGpuFlag). nullptr when the window has none (SDL's offscreen / dummy
// drivers): render_window then needs CLERIA_DRIVE's offscreen target.
#if defined(__APPLE__)
#include <mach-o/dyld.h>   // _NSGetExecutablePath
#endif
#ifndef _WIN32
// MenuFont (view_menus.inl) rasterises with stb_truetype where Windows uses GDI: Dear ImGui's vendored
// copy, compiled static into this translation unit. Its rect packer first, so stbrp_node is the real
// struct imgui_internal.h forward-declares (stb_truetype otherwise typedefs a stand-in of that name).
#define STBRP_STATIC
#define STB_RECT_PACK_IMPLEMENTATION
#include "imstb_rectpack.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"
static constexpr int FW_NORMAL = 400;   // the LOGFONT weight MenuFont takes (unused off Windows)
#endif

#if defined(__APPLE__)
static constexpr SDL_WindowFlags kWindowGpuFlag = SDL_WINDOW_METAL;
#else
static constexpr SDL_WindowFlags kWindowGpuFlag = 0;
#endif

static WGPUSurface create_window_surface(WGPUInstance instance, SDL_Window* win) {
    const SDL_PropertiesID props = SDL_GetWindowProperties(win);
    WGPUSurfaceDescriptor sd = {};
#if defined(_WIN32)
    HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    WGPUSurfaceSourceWindowsHWND srcw = {};
    srcw.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    srcw.hinstance = GetModuleHandle(nullptr); srcw.hwnd = hwnd;
    sd.nextInChain = &srcw.chain;
    return wgpuInstanceCreateSurface(instance, &sd);
#elif defined(__APPLE__)
    SDL_MetalView view = SDL_Metal_CreateView(win);
    void* layer = view ? SDL_Metal_GetLayer(view) : nullptr;
    if (!layer) { std::fprintf(stderr, "no CAMetalLayer: %s\n", SDL_GetError()); return nullptr; }
    WGPUSurfaceSourceMetalLayer src = {};
    src.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    src.layer = layer;
    sd.nextInChain = &src.chain;
    return wgpuInstanceCreateSurface(instance, &sd);
#else
    const char* drv = SDL_GetCurrentVideoDriver();
    if (drv && std::strcmp(drv, "wayland") == 0) {
        WGPUSurfaceSourceWaylandSurface src = {};
        src.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        src.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        src.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        if (!src.display || !src.surface) return nullptr;
        sd.nextInChain = &src.chain;
        return wgpuInstanceCreateSurface(instance, &sd);
    }
    if (drv && std::strcmp(drv, "x11") == 0) {
        WGPUSurfaceSourceXlibWindow src = {};
        src.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        src.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        src.window = std::uint64_t(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        if (!src.display || !src.window) return nullptr;
        sd.nextInChain = &src.chain;
        return wgpuInstanceCreateSurface(instance, &sd);
    }
    std::fprintf(stderr, "no WebGPU surface for SDL's \"%s\" video driver\n", drv ? drv : "?");
    return nullptr;
#endif
}

// This exe's full path (the .cleriamod association's open command); empty when unknown.
static fs::path platform_exe_path() {
#if defined(_WIN32)
    std::wstring buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    buf.resize(n);
    return fs::path(buf);
#elif defined(__APPLE__)
    std::uint32_t n = 0;
    _NSGetExecutablePath(nullptr, &n);
    std::string buf(n, '\0');
    if (_NSGetExecutablePath(buf.data(), &n) != 0) return {};
    buf.resize(std::strlen(buf.c_str()));
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(fs::path(buf), ec);
    return ec ? fs::path(buf) : p;
#else
    std::error_code ec;
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path() : p;
#endif
}
