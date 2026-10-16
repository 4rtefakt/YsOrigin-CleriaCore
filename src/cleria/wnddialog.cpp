// The event windows: build/event_re/WNDDIALOG_DRAW.md. See wnddialog.hpp.
#include "cleria/wnddialog.hpp"

#include <algorithm>
#include <cmath>

#include "cleria/wndtext.hpp"

namespace cleria {

namespace {

constexpr std::uint32_t kStd = 0x24041;

void quad(DrawList& out, TexRef tex, float x, float y, float w, float h, float u0, float v0, float u1,
          float v1, std::uint32_t diffuse, std::uint32_t specular = 0, std::uint32_t flags = kStd) {
    if ((diffuse >> 24) == 0) return;
    DrawCmd c;
    c.kind = DrawCmd::Quad;
    c.tex = tex;
    c.x = x; c.y = y; c.w = w; c.h = h;
    c.u0 = u0; c.v0 = v0; c.u1 = u1; c.v1 = v1;
    c.diffuse = diffuse; c.specular = specular; c.flags = flags;
    c.anchor_x = 2; c.anchor_y = 0;
    out.push_back(std::move(c));
}

TexRef named(const char* n) { return {TexRef::Named, 0, n}; }

// FUN_0057a410: the 36x36 "next" arrow, fading in then pulsing a warm specular glow.
void prompt(DrawList& out, int n, std::uint32_t D, float cx, float cy) {
    const int t = 12 * n;
    const std::uint32_t a = t < 255 ? std::uint32_t(t) : (D >> 24);
    const int e = (t >> 1) & 0x1ff;
    const int tri = e < 0x100 ? e : 0x200 - e;
    const std::uint32_t rg = std::uint32_t(tri / 3), b = std::uint32_t(tri / 16);
    quad(out, named(kFaceWin), cx - 18, cy - 18, 36, 36, 0.703125f, 0.5234375f, 0.7734375f, 0.59375f,
         (a << 24) | 0xFFFFFF, (rg << 16) | (rg << 8) | b);
}

void text(DrawList& out, const RichLayout& l, float x, float y, std::uint32_t colour, std::uint32_t reveal) {
    DrawCmd c;
    c.kind = DrawCmd::TextRich;
    c.x = x; c.y = y;
    c.diffuse = colour;
    c.rich = l.glyphs;
    c.reveal = reveal;
    c.anchor_x = 2; c.anchor_y = 0;
    out.push_back(std::move(c));
}

// The name line template 0x6a0998 (the LF at 0x6a09cc is a real 0x0a).
std::string name_line(const std::string& name) {
    return "<nocount><scale:.8><y:-4><color:0xfff0e0c0>" + name +
           "<count>\n<y:8><scale:1.0><y:0><color:0xffffffff>";
}

}  // namespace

// ---- WndText ---------------------------------------------------------------------------------

void WndText::message(int cls, int character, int frame, const std::string& name, const std::string& body,
                      const RichMetrics& m) {
    if (!alive_) { alive_ = true; box_ = 0; close_ = false; }
    // The face (FUN_00573e60 step 4).
    if (cls == 0xd) cls = character <= 1 ? 0xa6 : character == 2 ? 0xa7 : 0xa8;
    if (cls == 0 || frame == -1) { face_ = -1; frame_ = -1; }
    else if (cls >= 0xa6 && cls <= 0xae) { face_ = 0x3c + (cls - 0xa6); frame_ = frame; }
    else { face_ = cls - 0x56; if (face_ > 0x3b || face_ < 0) face_ = -1; frame_ = frame; }
    // FUN_0057b030.
    released_ = false; revealed_ = false; armed_ = false; fast_ = false; accepted_ = false;
    text_ = 1.0f; prompt_ = 0; tick_ = 0;
    reveal_ = (!body.empty() && box_ >= 1.0f) ? 0x10000u : 0u;
    layout_ = layout_rich(wnd_unescape(name_line(name) + body), m, 12.0f);
    reveal_end_ = std::uint32_t(layout_.counted) << 16;
}

void WndText::update(const WndInput& in, std::vector<int>& sounds) {
    if (!alive_) return;
    if (accepted_) {
        prompt_ = 0;
        if (!close_) text_ = std::max(0.0f, text_ - 0.05f);
        else {
            box_ -= fast_ ? 0.1f : 0.075f;
            if (box_ <= 0) { box_ = 0; alive_ = false; }
        }
        return;
    }
    if (box_ < 1.0f) {
        box_ += fast_ ? 0.1f : 0.075f;
        if (box_ < 1.0f) return;
    }
    box_ = 1.0f;
    if (close_ || in.cancel_repeat) fast_ = true;
    if (!in.confirm_held) released_ = true;
    if ((reveal_ & 0xffff0000u) < reveal_end_) {
        if (++tick_ > 4) { tick_ = 0; sounds.push_back(0x0c); }
        reveal_ += (fast_ || text_speed == 1) ? 0x50000u : speed_;
        if (text_speed == 2) reveal_ = std::max(reveal_, reveal_end_);   // CleriaCore: instant text
        if (close_ || (released_ && in.confirm_held)) fast_ = true;
        return;
    }
    revealed_ = true;
    if (armed_) ++prompt_;
    if (close_ || (armed_ && (in.cancel_repeat || (released_ && in.confirm_pressed)))) accepted_ = true;
}

void WndText::draw(DrawList& out, bool dialog_alive) const {
    if (!alive_) return;
    const std::uint32_t a = std::min(255u, std::uint32_t(std::floor(256.0f * box_)));
    const std::uint32_t D = (a << 24) | 0xFFFFFF;
    // The FACEWIN0 strip: x {0, 154, 768, 1024}, u {0, .25, .25, .80859375}, v 0..0.5, y 544..768.
    quad(out, named(kFaceWin), 0, 544, 154, 224, 0, 0, 0.25f, 0.5f, D);
    quad(out, named(kFaceWin), 154, 544, 614, 224, 0.25f, 0, 0.25f, 0.5f, D);
    quad(out, named(kFaceWin), 768, 544, 256, 224, 0.25f, 0, 0.80859375f, 0.5f, D);
    if (prompt_ != 0 && !dialog_alive) prompt(out, prompt_, D, 950, 728);
    float tx = 128;
    if (face_ != -1) {
        if (frame_ != -1) tx = 280;
        TexRef f{TexRef::Face, face_, nullptr};
        f.aux = frame_;
        quad(out, f, 47.5f, 517.5f, 230, 230, 0, 0, 1, 1, D);
    }
    if (reveal_ != 0) {
        const std::uint32_t ta = std::uint32_t(float(a) * text_);
        text(out, layout_, tx, 569.6f, (ta << 24) | 0xFFFFFF, reveal_);
    }
}

// ---- WndDialog -------------------------------------------------------------------------------

void WndDialog::treasure(float x, float y, int item, const std::string& t, const RichMetrics& m) {
    *this = WndDialog{};
    alive_ = true; treasure_ = true;
    x_ = x; y_ = y; item_ = item;
    texts_ = {wnd_unescape(t)};
    labels_ = {std::string()};
    lay_ = {layout_rich(texts_[0], m, 12.0f)};
    const float tw = lay_[0].width, th = lay_[0].height;
    const bool icon = item != 0xff;
    w_ = icon ? tw + 128.0f : tw + 38.4f;
    h_ = th + 25.6f;
    rx_ = {icon ? 89.6f : 0.0f};
    ry_ = {(h_ - th) * 0.5f};
    reveal_end_ = std::uint32_t(lay_[0].counted) << 16;
}

void WndDialog::menu(float x, float y, const std::string& title, const std::vector<std::string>& items,
                     const std::vector<std::string>& labels, const RichMetrics& m, bool centre_y, bool tick) {
    *this = WndDialog{};
    alive_ = true; treasure_ = false; tick_sound_ = tick;
    x_ = x; y_ = y;
    texts_.push_back(wnd_unescape(title));
    labels_.push_back(std::string());
    for (std::size_t i = 0; i < items.size(); ++i) {
        texts_.push_back(wnd_unescape(items[i]));
        labels_.push_back(i < labels.size() ? labels[i] : std::string());
    }
    layout(m);
    if (centre_y) y_ = (768.0f - h_) * 0.5f;
}

// FUN_00578710. The rows sit every 38.4 from 6.4 whatever the font measures [BYTES 0x6bda24, 0x6bd8a0; EXPORT
// 00578710.c:91-101]; only the box's height adds the measured heights.
void WndDialog::layout(const RichMetrics& m) {
    lay_.clear(); rx_.clear(); ry_.clear();
    widest_ = 0;
    float h = 0;
    for (std::size_t i = 0; i < texts_.size(); ++i) {
        lay_.push_back(layout_rich(texts_[i], m, 12.0f));
        rx_.push_back(-0.5f * lay_.back().width);
        widest_ = std::max(widest_, lay_.back().width);
        if (i == 0) ry_.push_back(-44.8f);
        else { ry_.push_back(float(i - 1) * 38.4f + 6.4f); h += lay_.back().height + 6.4f; }
    }
    w_ = widest_ + 41.6f;
    h_ = h + 12.8f;
    reveal_end_ = std::uint32_t(lay_[0].counted) << 16;
}

void WndDialog::update(const WndInput& in, std::vector<int>& sounds) {
    if (!alive_) return;
    if (accepted_) {
        alpha_ -= 0.075f;
        if (alpha_ <= 0) { alpha_ = 0; alive_ = false; }
        return;
    }
    const bool interactive = armed_;
    // Path A [BYTES 0x578a0e..0x578af1]: on the first interactive tick in mouse mode, snap the cursor
    // to the row under the pointer (before the fade-in completes, like retail).
    if (interactive && !was_interactive_ && in.mouse_mode && !treasure_) {
        const int row = mouse::wnddialog_row(in.mx, in.my, x_, y_, w_, h_, int(texts_.size()), false, in.display);
        if (row > 0) {
            if (row != cursor_) sounds.push_back(3);
            cursor_ = row;
            hover_ = true;
        }
    }
    was_interactive_ = interactive;
    if (alpha_ < 1.0f) { alpha_ = std::min(1.0f, alpha_ + 0.075f); return; }
    const bool held = in.confirm_held || (cancel_ >= 0 && in.cancel_held);
    if (!held) released_ = true;
    if (reveal_ < reveal_end_) {
        if (tick_sound_ && ++tick_ > 4) { tick_ = 0; sounds.push_back(0x0c); }
        reveal_ += (fast_ || text_speed == 1) ? 0x30000u : 0x7000u;
        if (text_speed == 2) reveal_ = std::max(reveal_, reveal_end_);   // CleriaCore: instant text
        // The fast latch +0x1fd4 is set after the step, so it speeds up the next tick [BYTES 0x578ef9..0x578f2a]:
        // a closing dialog (+0x1fd5), or an interactive one while held bit 0x1 (Attack) is down.
        if (close_ || (interactive && in.attack_held)) fast_ = true;
        return;
    }
    revealed_ = true;
    if (interactive) ++prompt_;
    if (close_) { armed_ = false; accepted_ = true; return; }
    if (treasure_) hover_ = true;                            // 0x578d1d: a click anywhere takes the item-get box
    if (!interactive || !released_) return;
    // Confirm on OK 0x2000, or on the click 0x10000 with the hover flag [BYTES 0x578bca..0x578be9].
    if (in.confirm_pressed || (in.click_pressed && hover_)) {
        accepted_ = true;
        sounds.push_back(0);
        chosen_ = treasure_ ? std::string() : labels_[std::size_t(cursor_)];
        chosen_index_ = cursor_;
        return;
    }
    if (in.cancel_pressed && cancel_ != -1) {
        accepted_ = true;
        sounds.push_back(1);
        chosen_index_ = std::clamp(cancel_, 0, int(labels_.size()) - 1);
        chosen_ = labels_[std::size_t(chosen_index_)];
        return;
    }
    if (!treasure_ && in.vstep != 0) {
        const int n = int(texts_.size());
        int c = cursor_ + in.vstep;
        if (c < 1) c = n - 1;
        if (c >= n) c = 1;
        cursor_ = c;
        sounds.push_back(3);                                  // SE 3 whenever vstep != 0 (0x578d41)
    }
    // Path B [BYTES 0x578d87..0x578e90]: on cursor motion (no mouse-mode test) the hover flag drops,
    // then the row under the pointer (with the left gate) takes the cursor.
    if (!treasure_ && in.mouse_moved) {
        hover_ = false;
        const int row = mouse::wnddialog_row(in.mx, in.my, x_, y_, w_, h_, int(texts_.size()), true, in.display);
        if (row > 0) {
            if (row != cursor_) sounds.push_back(3);
            cursor_ = row;
            hover_ = true;
        }
    }
}

void WndDialog::draw(DrawList& out) const {
    if (!alive_) return;
    const std::uint32_t a = std::min(255u, std::uint32_t(std::floor(256.0f * alpha_)));
    const std::uint32_t C = (a << 24) | 0xFFFFFF;
    const float L = x_ - std::floor(w_ / 2), T = y_;
    // DIALOG0 as a 4x4 grid: the caps are 1:1 texels outside the inner rect.
    const float xs[4] = {L - 80, L, L + w_, L + w_ + 80}, us[4] = {0, 0.15625f, 0.65625f, 0.8125f};
    const float ys[4] = {T - 64, T, T + h_, T + h_ + 32}, vs[4] = {0, 0.25f, 0.6875f, 0.8125f};
    for (int r = 0; r < 3 && !no_frame_; ++r)
        for (int c = 0; c < 3; ++c)
            quad(out, named(kDialog0), xs[c], ys[r], xs[c + 1] - xs[c], ys[r + 1] - ys[r], us[c], vs[r], us[c + 1],
                 vs[r + 1], C);
    float bx = x_;
    if (item_ != -1) {
        bx = L;
        if (item_ != 0xff) quad(out, {TexRef::Icon, item_, nullptr}, L + 19.2f, T + (h_ - 51.2f) / 2, 51.2f, 51.2f, 0, 0, 1, 1, C);
        if (prompt_ != 0) prompt(out, prompt_, C, L + w_ - 18.4f, T + ry_[0] + 80.0f);
    }
    for (std::size_t i = 0; i < texts_.size(); ++i) {
        if (i > 0 && !revealed_) break;
        std::uint32_t col = C;
        if (!treasure_ && i > 0 && int(i) != cursor_ && revealed_ && armed_) col = (a << 24) | 0xB0D0D0;
        text(out, lay_[i], bx + rx_[i], T + ry_[i], col, i == 0 ? reveal_ : 0x7fff0000u);
    }
    if (!treasure_ && revealed_ && armed_ && cursor_ >= 1 && cursor_ < int(texts_.size())) {
        const float W = widest_, cy = T + ry_[std::size_t(cursor_)];
        const float cxs[4] = {x_ - W / 2 - 32, x_ - W / 2, x_ + W / 2, x_ + W / 2 + 32};
        const float cus[4] = {0, 0.125f, 0.875f, 1};
        for (int k = 0; k < 3; ++k)
            quad(out, named(kMenuCursor), cxs[k], cy, cxs[k + 1] - cxs[k], 32, cus[k], 0, cus[k + 1], 1, C, 0, 0x40c1);
    }
}

}  // namespace cleria
