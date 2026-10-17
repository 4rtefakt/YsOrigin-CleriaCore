// cleria-view — the first-run setup screen (issue #46). A CleriaCore addition: retail has none.
// Textual part of main.cpp: #included after view_game.inl (Shell, the ui kit, create_window_surface);
// ONE translation unit, not standalone.
//
// Started with no asset root and no game data anywhere (no unpacked data folder, no development assets
// folder), cleria-view printed "no default room" and exited: a new user had to know cleria-gamedata. It now
// opens this screen instead: the Ys Origin installs found in the Steam libraries (one row each), a folder
// dialog, a typed folder, the unpack with its progress, Quit. Keyboard, mouse and pad (the kit's ui::Nav).
// The unpack is gamedata::InstallTask, the one the F1 panel's Prelaunch page runs; that page's GameDataUi
// lives inside render_window, so its few lines of glue are repeated here.
//
// Returns the data folder once it is Ready (the start then goes on with it), or an empty path: the user quit.
//
// CLERIA_SETUP_DRIVE="<frame>:<cmd>;..." runs it with no display (a hidden window, an offscreen target;
// CLERIA_DRIVE_VISIBLE=1 shows the window and draws into its surface, as CLERIA_DRIVE does):
//   find            the first valid install found, as its row does
//   locate=<dir>    as if the folder dialog answered <dir>
//   cancel          the Cancel row
//   shot=<file>     this frame as a PPM
//   quit            leave (the start stops, exit code 0)
// Each command prints "[setup] t<frame> <cmd>"; the screen prints "[setup] ..." for what it does.

static bool has_game_data(const fs::path& root) {
    std::error_code ec;
    return !root.empty() && fs::exists(root / "MAP", ec);
}

static fs::path first_run_setup(Gpu& g) {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return {}; }
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) std::fprintf(stderr, "SDL gamepad: %s\n", SDL_GetError());
    const char* script = std::getenv("CLERIA_SETUP_DRIVE");
    const bool hidden = script != nullptr && !std::getenv("CLERIA_DRIVE_VISIBLE");
    SDL_Window* win = SDL_CreateWindow("CleriaCore - Setup", 1280, 800,
                                       SDL_WINDOW_RESIZABLE | kWindowGpuFlag | (hidden ? SDL_WINDOW_HIDDEN : 0));
    if (!win) { std::fprintf(stderr, "SDL window: %s\n", SDL_GetError()); return {}; }
    WGPUSurface surface = hidden ? nullptr : create_window_surface(g.instance, win);
    WGPUSurfaceCapabilities caps = {};
    if (surface) wgpuSurfaceGetCapabilities(surface, g.adapter, &caps);
    const bool offscreen = hidden || caps.formatCount == 0;
    if (offscreen && !hidden) {
        std::fprintf(stderr, "no presentable surface for the setup screen: run cleria-gamedata --install <Ys Origin folder>\n");
        SDL_DestroyWindow(win);
        return {};
    }
    if (script && surface)
        std::printf("[setup] surface: %zu format(s), usages 0x%llx\n", caps.formatCount, (unsigned long long)caps.usages);
    WGPUTextureFormat fmt = offscreen ? WGPUTextureFormat_RGBA8Unorm : caps.formats[0];
    for (std::size_t i = 0; i < caps.formatCount; ++i)      // the linear format, as render_window picks it
        if (caps.formats[i] == WGPUTextureFormat_RGBA8Unorm || caps.formats[i] == WGPUTextureFormat_BGRA8Unorm) {
            fmt = caps.formats[i];
            break;
        }
    std::uint32_t cw = 1280, ch = 800;
    WGPUTexture off_tex = nullptr;
    auto configure = [&] {
        if (offscreen) {
            if (off_tex) wgpuTextureRelease(off_tex);
            WGPUTextureDescriptor td = {};
            td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
            td.dimension = WGPUTextureDimension_2D; td.size = {cw, ch, 1};
            td.format = fmt; td.mipLevelCount = 1; td.sampleCount = 1;
            off_tex = wgpuDeviceCreateTexture(g.device, &td);
            return;
        }
        WGPUSurfaceConfiguration sc = {};
        sc.device = g.device; sc.format = fmt; sc.usage = WGPUTextureUsage_RenderAttachment;
        if (script) sc.usage |= WGPUTextureUsage_CopySrc;   // a driven run's `shot`
        sc.width = cw; sc.height = ch; sc.alphaMode = WGPUCompositeAlphaMode_Auto;
        sc.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(surface, &sc);
    };
    configure();

    struct Setup {
        gamedata::InstallTask task;
        std::vector<gamedata::Validation> found;   // the valid installs in the Steam libraries
        std::string msg;
        bool msg_ok = false, was_running = false, ready = false, quit = false;
        std::mutex mu;
        std::string picked;                        // set by the folder dialog's callback
        std::string typed;                         // the Folder row
        ui::List list;

        void start(const fs::path& dir) {
            const gamedata::Validation v = gamedata::validate(dir);
            if (!v.ok) {
                msg = "Not a Ys Origin folder" + (v.missing.empty() ? std::string() : ": missing " + v.missing[0]);
                msg_ok = false;
                std::printf("[setup] %s (%s)\n", msg.c_str(), dir.string().c_str());
                return;
            }
            msg.clear();
            std::printf("[setup] unpacking %s into %s\n", v.root.string().c_str(), gamedata::data_dir().string().c_str());
            if (!task.start(v.root)) { msg = "Could not start unpacking."; msg_ok = false; }
        }
        void poll() {
            std::string p;
            { std::lock_guard<std::mutex> lk(mu); p.swap(picked); }
            if (!p.empty()) start(fs::u8path(p));
            const bool run = task.running();
            if (was_running && !run) {             // just finished
                const gamedata::InstallResult r = task.result();
                if (r.ok && !gamedata::ready_data_dir().empty()) {
                    std::printf("[setup] unpacked %llu files in %.0f s\n", (unsigned long long)r.files, r.seconds);
                    ready = true;
                } else {
                    msg_ok = false;
                    msg = r.cancelled ? "Unpacking cancelled." : "Unpacking failed: " + (r.error.empty() ? std::string("the data folder is not complete") : r.error);
                    std::printf("[setup] %s\n", msg.c_str());
                }
            }
            was_running = run;
        }
    };
    static Setup s;   // static: the folder dialog may answer after this function has returned
    for (gamedata::Validation& v : gamedata::find_installs())
        if (v.ok) s.found.push_back(std::move(v));
    std::printf("[setup] no game data: the setup screen (%zu install(s) found)\n", s.found.size());

    Shell shell;
    shell.init(win, g, fmt);
    shell.launcher = true;                         // the screen owns the input
    shell.under = [&](ImDrawList* dl, float w, float h, const ui::Nav& nav) {
        const float S = ui::S;
        const float pw = std::min(w - 48.0f * S, 980.0f * S), ph = std::min(h - 48.0f * S, 760.0f * S);
        const ImVec2 a{(w - pw) * 0.5f, (h - ph) * 0.5f}, b{a.x + pw, a.y + ph};
        dl->AddRectFilled(a, b, ui::kPanel, 8.0f * S);
        dl->AddRect(a, b, ui::kPanelEdge, 8.0f * S, 0, 1.2f * S);
        const float x = a.x + 28.0f * S, tw = pw - 56.0f * S;
        float y = a.y + 24.0f * S;
        ui::text(dl, ui::F.bold, 34.0f * S, {x, y}, ui::kTabOn, "CLERIACORE");
        y += 44.0f * S;
        ui::text(dl, ui::F.cond, 20.0f * S, {x, y}, ui::kGold, "SET UP THE GAME DATA");
        y += 34.0f * S;
        const std::string intro =
            "CleriaCore plays your own copy of Ys Origin. Choose its install folder (the one with yso_win.exe): "
            "the game's data is unpacked from it into CleriaCore's data folder, about 3 GB, in well under a minute. "
            "Your install is only read, never changed.";
        const float ipx = 17.5f * S;
        dl->AddText(ui::F.body, ipx, {x, y}, ui::kDesc, intro.c_str(), nullptr, tw);
        y += ui::F.body->CalcTextSizeA(ipx, FLT_MAX, tw, intro.c_str()).y + 16.0f * S;
        dl->AddLine({a.x, y}, {b.x, y}, ui::kRule, 1.0f * S);
        y += 10.0f * S;
        const float desc_h = 74.0f * S;
        ui::List& L = s.list;
        ui::list_begin(L, dl, {x, y}, {tw, b.y - y - desc_h - 8.0f * S}, nav, true);
        if (s.task.running()) {
            const gamedata::Progress pr = s.task.progress();
            char buf[96];
            std::snprintf(buf, sizeof buf, "Unpacking...  %d%%  (%llu / %llu files)",
                          pr.bytes_total ? int(pr.bytes_done * 100 / pr.bytes_total) : 0,
                          (unsigned long long)pr.files_done, (unsigned long long)pr.files_total);
            ui::info(L, "Status", buf, ui::kWarn);
            if (ui::button(L, "Cancel", "Stop unpacking. Nothing in your Ys Origin install is touched.")) s.task.cancel();
        } else {
            ui::section(L, "Found in your Steam libraries", ui::kSection);
            if (s.found.empty())
                ui::info(L, "", "No Ys Origin install was found", ui::kMuted);
            for (const gamedata::Validation& v : s.found)
                if (ui::button(L, ("Use  " + v.root.string()).c_str(), "Unpack the game data from this install."))
                    s.start(v.root);
            ui::section(L, "Somewhere else", ui::kSection);
            if (ui::button(L, "Locate Ys Origin...", "Pick your Ys Origin install folder in a file dialog."))
                SDL_ShowOpenFolderDialog([](void* u, const char* const* list, int) {
                    auto* self = static_cast<Setup*>(u);
                    if (!list || !list[0]) return;      // cancelled or failed
                    std::lock_guard<std::mutex> lk(self->mu);
                    self->picked = list[0];
                }, &s, win, nullptr, false);
            ui::text_field(L, "Folder", &s.typed,
                           "Type or paste the install folder here when no file dialog opens (a Steam Deck in game "
                           "mode), then choose \"Use the typed folder\".", false, "(not set)", 512);
            if (ui::button(L, "Use the typed folder", "Unpack the game data from the folder typed above.", !s.typed.empty()))
                s.start(fs::u8path(s.typed));
        }
        if (!s.msg.empty()) ui::info(L, "", s.msg.c_str(), s.msg_ok ? ui::kGood : ui::kWarn);
        ui::info(L, "Data folder", gamedata::data_dir().string().c_str(), ui::kValue);
        if (!s.task.running()) {
            ui::section(L, "");
            if (ui::button(L, "Quit", "Close CleriaCore. Nothing was changed.")) s.quit = true;
        }
        ui::list_end(L);
        dl->AddLine({a.x, b.y - desc_h}, {b.x, b.y - desc_h}, ui::kDivider, 1.0f * S);
        if (!L.desc.empty())
            dl->AddText(ui::F.body, 16.5f * S, {x, b.y - desc_h + 12.0f * S}, ui::kDesc, L.desc.c_str(), nullptr, tw);
    };

    // CLERIA_SETUP_DRIVE: "<frame>:<cmd>;..."
    std::vector<std::pair<long, std::string>> cmds;
    if (script) {
        std::string all = script;
        for (std::size_t at = 0; at < all.size();) {
            const std::size_t end = std::min(all.find(';', at), all.size());
            const std::string one = all.substr(at, end - at);
            if (const std::size_t c = one.find(':'); c != std::string::npos)
                cmds.emplace_back(std::atol(one.c_str()), one.substr(c + 1));
            at = end + 1;
        }
    }

    fs::path result;
    for (long frame = 0; !s.quit; ++frame) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) s.quit = true;
            else if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                cw = std::uint32_t(e.window.data1); ch = std::uint32_t(e.window.data2);
                if (cw && ch) configure();
            } else if (!(e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F1)) shell.event(e);   // no F1 panel here
        }
        fs::path shot;
        for (const auto& [at, cmd] : cmds) {
            if (at != frame) continue;
            std::printf("[setup] t%ld %s\n", frame, cmd.c_str());
            if (cmd == "find") { if (s.found.empty()) std::printf("[setup] no install found\n"); else s.start(s.found[0].root); }
            else if (cmd.rfind("locate=", 0) == 0) { std::lock_guard<std::mutex> lk(s.mu); s.picked = cmd.substr(7); }
            else if (cmd == "cancel") s.task.cancel();
            else if (cmd.rfind("shot=", 0) == 0) shot = fs::u8path(cmd.substr(5));
            else if (cmd == "quit") s.quit = true;
        }
        s.poll();
        if (s.ready) { result = gamedata::ready_data_dir(); break; }
        if (!cw || !ch) { SDL_Delay(16); continue; }

        WGPUSurfaceTexture st = {};
        if (offscreen) { st.texture = off_tex; wgpuTextureAddRef(off_tex); st.status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal; }
        else wgpuSurfaceGetCurrentTexture(surface, &st);
        if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            static int last_status = 0;
            if (script && int(st.status) != last_status)   // a driven run says why its frames are missing
                std::printf("[setup] t%ld no surface texture (status 0x%x)\n", frame, unsigned(last_status = int(st.status)));
            if (surface_retry(st.status)) configure();
            continue;
        }
        WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);
        {   // the backdrop: one flat colour (there is no game data to draw a room from yet)
            WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
            WGPURenderPassColorAttachment ca = {};
            ca.view = view; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            ca.loadOp = WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store; ca.clearValue = {0.045, 0.043, 0.035, 1};
            WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
            wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
            wgpuQueueSubmit(g.queue, 1, &cmd);
            wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
        }
        shell.frame(g, view, cw, ch);
        if (!shot.empty()) save_frame_ppm(g, st.texture, fmt, cw, ch, shot);
        wgpuTextureViewRelease(view);
        if (!offscreen) wgpuSurfacePresent(surface);
        wgpuTextureRelease(st.texture);
        if (offscreen) SDL_Delay(script ? 1 : 16);
    }
    if (result.empty()) std::printf("[setup] quit: no game data\n");
    else std::printf("[setup] ready: %s\n", result.string().c_str());

    s.task.cancel();
    s.task.wait();
    shell.under = nullptr;
    shell.shutdown();
    if (off_tex) wgpuTextureRelease(off_tex);
    if (surface) { wgpuSurfaceUnconfigure(surface); wgpuSurfaceRelease(surface); }
    SDL_DestroyWindow(win);
    return result;
}
