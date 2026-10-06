// The shell: CleriaCore's own layer around the game (build/shell_re/CONFIG.md §7) — the launcher's
// main menu and the F1 settings panel, drawn with the view_ui.inl kit. It is NOT part of the
// faithful game (retail's settings live in the separate `yso_win.exe -c` dialog), so it is drawn on
// top of the finished frame, after every capture (crossfade, movie / drive shots).
#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_wgpu.h"
#include "view_ui.inl"

// Read a finished frame back and write it as a PPM (headless captures of the shell).
static void save_frame_ppm(Gpu& g, WGPUTexture tex, WGPUTextureFormat fmt, std::uint32_t w, std::uint32_t h,
                           const fs::path& out) {
    const std::uint32_t bpr = (w * 4 + 255) & ~255u;
    WGPUBufferDescriptor rbd = {};
    rbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead; rbd.size = std::size_t(bpr) * h;
    WGPUBuffer rb = wgpuDeviceCreateBuffer(g.device, &rbd);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPUTexelCopyTextureInfo src = {}; src.texture = tex; src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst = {}; dst.buffer = rb;
    dst.layout.bytesPerRow = bpr; dst.layout.rowsPerImage = h;
    WGPUExtent3D ext = {w, h, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    MapReq mr;
    WGPUBufferMapCallbackInfo mi = {};
    mi.mode = WGPUCallbackMode_AllowProcessEvents; mi.callback = on_map; mi.userdata1 = &mr;
    wgpuBufferMapAsync(rb, WGPUMapMode_Read, 0, rbd.size, mi);
    while (!mr.done) wgpuDevicePoll(g.device, true, nullptr);
    const auto* px = static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(rb, 0, rbd.size));
    const bool bgra = fmt == WGPUTextureFormat_BGRA8Unorm || fmt == WGPUTextureFormat_BGRA8UnormSrgb;
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::uint8_t* q = px + std::size_t(y) * bpr + x * 4;
            const char rgb[3] = {char(q[bgra ? 2 : 0]), char(q[1]), char(q[bgra ? 0 : 2])};
            f.write(rgb, 3);
        }
    wgpuBufferUnmap(rb);
    wgpuBufferRelease(rb);
}

struct Shell {
    bool ready = false;
    bool open = false;            // the settings panel (F1)
    bool launcher = false;        // the launcher owns the screen: input always goes to the shell
    ui::Panel panel;
    // Drawn under the panel every frame while set (the launcher's main menu). `nav` is empty while
    // the panel is open, so the menu does not react to the panel's keys.
    std::function<void(ImDrawList*, float, float, const ui::Nav&)> under;
    // Always-on layers over the game, drawn whether or not the panel is open (a speedrun timer, a
    // tracker, the cutscene-skip prompt), over `under` and under the panel. Keyed by feature so each
    // can set or erase its own; a layer draws nothing when it has nothing to show.
    std::map<std::string, std::function<void(ImDrawList*, float, float)>> hud;

    void init(SDL_Window* win, Gpu& g, WGPUTextureFormat fmt) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;                          // no imgui.ini next to the exe
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // feeds the pad keys; ImGui's own nav is unused
        ui::load_fonts(exe_dir() / "fonts");
        ImGui_ImplSDL3_InitForOther(win);
        ImGui_ImplWGPU_InitInfo ii;
        ii.Device = g.device;
        ii.NumFramesInFlight = 3;
        ii.RenderTargetFormat = fmt;
        ii.DepthStencilFormat = WGPUTextureFormat_Undefined;
        ready = ImGui_ImplWGPU_Init(&ii);
    }
    void shutdown() {
        if (!ready) return;
        ImGui_ImplWGPU_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        ready = false;
    }

    // Feed one SDL event. Returns true when the shell consumed it (the game must not see it): the
    // F1 toggle, and every input event while the panel or the launcher owns the screen. Window /
    // quit events always pass through.
    // Rebinding: the next raw input of the asked kind (Configure Inputs). Esc cancels, Delete /
    // Backspace unbinds; F1 is never captured (input::bindable).
    struct Capture {
        enum Kind { Key, Mouse, Pad } kind = Key;
        int slot = -1;
        bool active = false, done = false, cancelled = false, cleared = false;
        int value = 0;            // an SDL scancode / mouse button / gamepad button (or kSdl*Trigger)
        std::string label;        // the action being bound, for the prompt
        void start(Kind k, int s, const std::string& what) {
            *this = Capture{}; kind = k; slot = s; label = what; active = true;
        }
    } capture;

    bool capture_event(const SDL_Event& e) {
        auto finish = [&](int v) { capture.value = v; capture.active = false; capture.done = true; };
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            if (e.key.key == SDLK_ESCAPE) { capture.cancelled = true; finish(0); return true; }
            if (e.key.key == SDLK_DELETE || e.key.key == SDLK_BACKSPACE) { capture.cleared = true; finish(0); return true; }
            if (capture.kind == Capture::Key && e.key.key != SDLK_F1) finish(int(e.key.scancode));
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (capture.kind == Capture::Mouse) finish(int(e.button.button));
            return true;
        }
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            if (capture.kind == Capture::Pad) finish(int(e.gbutton.button));
            return true;
        }
        if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && capture.kind == Capture::Pad && e.gaxis.value > 16000) {
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) finish(input::kSdlLeftTrigger);
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) finish(input::kSdlRightTrigger);
            return true;
        }
        // releases still reach ImGui, or the key that opened the prompt (OK) stays held there and its
        // next press is lost
        if (e.type == SDL_EVENT_KEY_UP || e.type == SDL_EVENT_MOUSE_BUTTON_UP ||
            e.type == SDL_EVENT_GAMEPAD_BUTTON_UP || e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            return true;
        }
        return false;
    }

    bool event(const SDL_Event& e) {
        if (!ready) return false;
        if (capture.active && (open || launcher) && capture_event(e)) return true;
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F1 && !e.key.repeat) {
            open = !open;
            g_shell_focus = open || launcher;
            return true;
        }
        if (!open && !launcher) return false;
        ImGui_ImplSDL3_ProcessEvent(&e);
        switch (e.type) {
            case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT:
            case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_WHEEL:
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                return true;
            default:
                return false;
        }
    }

    // Draw the shell over `view` (the finished frame) when the panel is open or the launcher is up.
    void frame(Gpu& g, WGPUTextureView view, std::uint32_t w, std::uint32_t h) {
        g_shell_focus = ready && (open || launcher);       // every frame, so it can never stay stuck on
        if (!ready || (!open && !under && hud.empty()) || w == 0 || h == 0) return;
        ui::S = float(h) / 1000.0f;                       // [H] the reference shots were 1125 px tall
        ImGui_ImplWGPU_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const ui::Nav nav = capture.active ? ui::Nav{} : ui::read_nav();   // a capture owns the keys
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const bool was_open = open;
        if (under) under(dl, float(w), float(h), open ? ui::Nav{} : nav);
        for (auto& [name, layer] : hud) layer(dl, float(w), float(h));
        if (was_open || !open) {      // opened by the launcher menu just now: the panel starts next frame,
            panel.open = open;        // or the same OK / click would also act on its selected row
            panel.draw(dl, float(w), float(h), nav);
            open = panel.open;
        }
        if (open && capture.active) {
            static const char* kWhat[] = {"Press a key", "Click a mouse button", "Press a gamepad button"};
            ui::prompt(dl, float(w), float(h), kWhat[int(capture.kind)], capture.label.c_str(),
                       "Esc cancels  â¢  Delete clears the binding");
        }
        g_shell_focus = open || launcher;
        ImGui::Render();

        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
        WGPURenderPassColorAttachment ca = {};
        ca.view = view;
        ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Load;                       // over the finished frame
        ca.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor rp = {};
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &ca;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuQueueSubmit(g.queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd);
        wgpuCommandEncoderRelease(enc);
    }
};
