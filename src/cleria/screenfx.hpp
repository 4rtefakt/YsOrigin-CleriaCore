// The screen effects of FUN_00427c90 / FUN_005fed30 (build/render_re/SCREEN_EFFECTS.md §7): state only,
// the passes are the view's (apps/cleria-view/view_screenfx.inl).
#pragma once

namespace cleria::screenfx {

// ZoomBlur (SCREEN_EFFECTS.md §7.1). XSO 0xce and the native callers write s (0x76a550) and r
// (0x76a554); FUN_00427c90 steps them once per update [EXPORT 00427c90.c:21-43]. The strength IS the
// opacity of the draw; there is no separate duration: it lasts s / |r| updates.
struct ZoomBlur {
    float s = 0.0f, r = 0.0f;            // 0x76a550 / 0x76a554
    float strength = 0.0f;               // 0x7b2490
    int taps = 0;                        // 0x7b2494
    float step_k = 0.0f;                 // 0x7b2498
    // 0x7b24a4 / 0x7b24a8, kept as a fraction of the frame (W, H). Reset to the centre ONLY when a
    // blur ends [EXPORT 00427c90.c:28-29].
    float cx = 0.5f, cy = 0.5f;

    void set(float s_, float r_) { s = s_; r = r_; }
    bool running() const { return s != 0.0f; }        // the [0x76a550] != 0.0 test of the boss modules
    void step() {
        if (s == 0.0f) return;
        s += r;
        if (s <= 0.0f) {
            s = 0.0f; strength = 0.0f; taps = 0; step_k = 0.0f;
            cx = cy = 0.5f;                            // (W >> 1, H >> 1)
        } else {
            strength = s;
            if (s > 1.0f) { s = 1.0f; strength = 1.0f; }   // 0x6bd438
            taps = 4;
            step_k = 0.02f;                            // 0x3ca3d70a
        }
    }
    // TEXTUREFACTOR alpha [BYTES 0x5ff10b..]: floor(strength * 256) below 1.0, 255 at 1.0.
    int alpha8() const { return strength >= 1.0f ? 255 : int(strength * 256.0f); }
    bool draws() const { return strength != 0.0f && taps != 0; }
};

}  // namespace cleria::screenfx
