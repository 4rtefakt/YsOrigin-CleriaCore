// The in-game panel: the test list (grouped buttons), the selected test's
// instructions / config buttons / helpers / answer buttons, and the helper page.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "probe.h"
#include "tests.h"

namespace panel {

static bool g_open = true;
static TestFile g_file;
static int g_sel = -1;
static std::string g_msg = "Pick a test. HOME hides this panel.";
static std::string g_last_answer;
static char g_note[512] = "";
static std::vector<std::pair<std::string, int>> g_cfg_pick;   // config values picked for the selected test
static char g_room[32] = "S_1000";
static bool g_room_has_pos = false;
static float g_room_pos[3] = {0, 0, 0};
static int g_flag_idx = 205, g_flag_val = 0;

static std::string tests_path() { return std::string(module_dir()) + "retail_probe_tests.ini"; }
static void reload() {
    load_tests(tests_path().c_str(), g_file);
    g_sel = -1;
    g_cfg_pick.clear();
    plog("tests: %d loaded from %s, %d problem(s)", (int)g_file.tests.size(), g_file.path.c_str(), (int)g_file.errors.size());
    for (const std::string& e : g_file.errors) plog("tests: %s", e.c_str());
}

void init() { reload(); }
void toggle() {
    g_open = !g_open;
    if (!g_open) ImGui::GetIO().MouseDrawCursor = false;
}
bool is_open() { return g_open; }
bool wants_text() { return g_open && ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput; }
int toggle_vk() { return g_file.toggle_vk ? g_file.toggle_vk : VK_HOME; }

// ---- the answers file -------------------------------------------------------------------
static void one_line(std::string& s) {
    for (char& c : s) if (c == '\n' || c == '\r' || c == '|') c = ' ';
}
static void write_answer(const Test& t, const char* answer) {
    const probe::Status& s = probe::status();
    std::string path = std::string(module_dir()) + "retail_answers.txt";
    FILE* f = fopen(path.c_str(), "a");
    if (!f) {
        g_msg = "Could not open retail_answers.txt for writing.";
        return;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::string note = g_note, q = t.question;
    one_line(note);
    one_line(q);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d | %s | answer: %s | note: %s | at %s (%.1f, %.1f, %.1f) %s %s Lv%d | "
               "shadows %u reflections %u glare %u other %u | Q: %s\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, t.id.c_str(), answer, note.c_str(),
            s.room[0] ? s.room : "-", s.pos[0], s.pos[1], s.pos[2], char_name(s.character), diff_name(s.difficulty),
            s.level, s.cfg_shadows, s.cfg_water, s.cfg_glare, s.cfg_other, q.c_str());
    fclose(f);
    plog("answer: %s = %s (%s)", t.id.c_str(), answer, note.c_str());
    g_last_answer = t.id + ": " + answer + (note.empty() ? "" : " - " + note);
    g_msg = "Answer saved to retail_answers.txt.";
    g_note[0] = 0;
}

// ---- widgets -------------------------------------------------------------------------------
// A full-width button whose label wraps.
static bool wrapped_button(int id, const char* text, bool selected, bool dim) {
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, w - 16.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    bool clicked = ImGui::Button("##t", ImVec2(w, ts.y + 10.0f));
    if (selected) ImGui::PopStyleColor();
    ImGui::PopID();
    ImU32 col = ImGui::GetColorU32(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    ImGui::GetWindowDrawList()->AddText(nullptr, 0.0f, ImVec2(pos.x + 8.0f, pos.y + 5.0f), col, text, nullptr, w - 16.0f);
    return clicked;
}
// A button that continues the current line when it fits and starts a new one when
// it does not. `first` = nothing is on the line yet.
static bool flow_button(const char* label, bool first) {
    if (!first) {
        float need = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + need <= right) ImGui::SameLine();
    }
    return ImGui::Button(label);
}
static void labelled(const char* label, const std::string& text) {
    if (text.empty()) return;
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", text.c_str());
}

static void draw_status() {
    const probe::Status& s = probe::status();
    if (s.in_room) {
        ImGui::Text("Room %s   x %.2f   y %.2f   z %.2f   heading %.2f", s.room[0] ? s.room : "?", s.pos[0], s.pos[1], s.pos[2], s.heading);
        ImGui::Text("%s  %s  Lv %d  HP %.0f/%.0f  MP %.0f%%  Boost %.0f%%  SP %d  weapon tier %d", char_name(s.character),
                    diff_name(s.difficulty), s.level, s.hp, s.max_hp, s.max_mp > 0 ? 100.0f * s.mp / s.max_mp : 0.0f,
                    s.boost / 300.0f, s.sp, s.weapon_tier);
    } else {
        ImGui::TextDisabled("Not in a room (title screen or loading). Load a save to start.");
    }
    ImGui::TextDisabled("Shadows %u  Reflections %u  Glare %u  Other simplification %u  Caustics %s", s.cfg_shadows, s.cfg_water,
                        s.cfg_glare, s.cfg_other, s.cfg_caustics ? "off" : "on");
    if (s.dirty && !probe::g_allow_save)
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "A test state is loaded: saving is BLOCKED (%d attempt(s) stopped). Do not rely on this save.",
                           s.saves_blocked);
    else if (s.dirty)
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Saving is ALLOWED with a test state loaded: do not save over a real slot.");
    const char* r = probe::why_not();
    if (r) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", r);
    if (!r || g_msg != r) ImGui::TextWrapped("%s", g_msg.c_str());
    ImGui::Separator();
}

static std::string needs_line(const Test& t) {
    std::string s;
    auto add = [&](const std::string& x) { s += (s.empty() ? "" : " | ") + x; };
    if (t.character >= 0) add(char_name(t.character));
    if (t.difficulty >= 0) add(diff_name(t.difficulty));
    if (!t.room.empty()) add("room " + t.room);
    if (t.has_pos) {
        char b[80];
        snprintf(b, sizeof(b), "at %.1f, %.1f, %.1f", t.pos[0], t.pos[1], t.pos[2]);
        add(b);
    }
    if (t.level > 0) add("level " + std::to_string(t.level));
    if (t.weapon_tier >= 0) add("weapon tier " + std::to_string(t.weapon_tier));
    if (!t.items.empty()) add(std::to_string(t.items.size()) + " item(s)");
    if (!t.flags.empty()) add(std::to_string(t.flags.size()) + " flag(s)");
    return s;
}

static void draw_selected() {
    if (g_sel < 0 || g_sel >= (int)g_file.tests.size()) {
        ImGui::TextDisabled("Click a question above: the game loads the room and sets the state for it.");
        return;
    }
    const Test& t = g_file.tests[g_sel];
    ImGui::TextDisabled("%s  [%s]%s%s", t.group.c_str(), t.id.c_str(), t.status.empty() ? "" : "  - ", t.status.c_str());
    ImGui::TextWrapped("%s", t.question.c_str());
    labelled("Do:", t.todo);
    labelled("Watch:", t.watch);
    labelled("Note:", t.note);
    labelled("Sets:", needs_line(t));
    if (const char* b = probe::test_blocker(t)) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", b);

    if (ImGui::Button("Go again")) probe::go(t, &g_cfg_pick, g_msg);
    for (const std::string& h : t.helpers) {
        const char* label = h == "boss_hp_1" ? "Boss HP -> 1" : h == "monsters_hp_1" ? "Monsters HP -> 1" :
                            h == "invincible" ? (probe::g_invincible ? "Invincible: ON" : "Invincible: off") :
                            h == "hp_lock" ? (probe::g_hp_lock ? "HP lock: ON" : "HP lock: off") :
                            h == "boost" ? "Fill Boost" : h == "heal" ? "Heal" : h == "mp" ? "Fill MP" : h.c_str();
        ImGui::PushID(h.c_str());
        if (flow_button(label, false)) probe::run_helper(h, g_msg);
        ImGui::PopID();
    }
    for (const CfgButton& cb : t.cfg_buttons) {
        ImGui::TextDisabled("%s:", cb.name.c_str());
        const char* note = "";
        for (int v : cb.values) {
            ImGui::SameLine();
            char lbl[48];
            snprintf(lbl, sizeof(lbl), "%d##%s", v, cb.name.c_str());
            note = probe::config_note(cb.name, v);
            bool restart = strncmp(note, "needs a restart", 15) == 0;
            if (restart) ImGui::BeginDisabled();
            if (ImGui::Button(lbl)) {
                bool found = false;
                for (auto& p : g_cfg_pick) if (p.first == cb.name) { p.second = v; found = true; }
                if (!found) g_cfg_pick.push_back({cb.name, v});
                probe::go(t, &g_cfg_pick, g_msg);
            }
            if (restart) ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", note);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(reloads the room; hover a value)");
    }
    for (int f : t.show_flags) {
        ImGui::SameLine();
        ImGui::TextDisabled("flag %d = %d", f, probe::get_flag(f));
    }

    ImGui::Separator();
    ImGui::TextDisabled("Answer (click one - it is written to retail_answers.txt):");
    for (size_t i = 0; i < t.answers.size(); i++) {
        ImGui::PushID((int)i);
        if (flow_button(t.answers[i].c_str(), i == 0)) write_answer(t, t.answers[i].c_str());
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150.0f);
    ImGui::InputTextWithHint("##note", "optional note - saved with the answer you click", g_note, sizeof(g_note));
    ImGui::SameLine();
    if (ImGui::Button("Save note only")) write_answer(t, "(note)");
    if (!g_last_answer.empty()) ImGui::TextDisabled("Last saved: %s", g_last_answer.c_str());
}

static void draw_tests() {
    float avail = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("list", ImVec2(0, avail * 0.52f), true);
    for (const std::string& e : g_file.errors) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", e.c_str());
    std::vector<std::string> groups;
    for (const Test& t : g_file.tests) {
        bool seen = false;
        for (const std::string& g : groups) seen = seen || g == t.group;
        if (!seen) groups.push_back(t.group);
    }
    for (const std::string& g : groups) {
        if (!ImGui::CollapsingHeader(g.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) continue;
        for (int i = 0; i < (int)g_file.tests.size(); i++) {
            const Test& t = g_file.tests[i];
            if (t.group != g) continue;
            bool blocked = probe::test_blocker(t) != nullptr;
            std::string label = t.question;
            if (t.character >= 0) label = std::string("[") + char_name(t.character) + "] " + label;
            if (wrapped_button(i, label.c_str(), i == g_sel, blocked)) {
                if (g_sel != i) g_cfg_pick.clear();
                g_sel = i;
                probe::go(t, &g_cfg_pick, g_msg);
            }
        }
    }
    ImGui::EndChild();
    ImGui::BeginChild("sel", ImVec2(0, 0), true);
    draw_selected();
    ImGui::EndChild();
}

static void draw_helpers() {
    ImGui::Checkbox("Invincible (hits pass through)", &probe::g_invincible);
    ImGui::SameLine();
    ImGui::Checkbox("HP lock (hits land, HP refills)", &probe::g_hp_lock);
    if (ImGui::Button("Full heal")) probe::heal(g_msg);
    ImGui::SameLine();
    if (ImGui::Button("Fill MP")) probe::refill_mp(g_msg);
    ImGui::SameLine();
    if (ImGui::Button("Fill Boost")) probe::refill_boost(g_msg);
    ImGui::SameLine();
    if (ImGui::Button("Boss HP -> 1")) probe::boss_hp_1(g_msg);
    ImGui::SameLine();
    if (ImGui::Button("Monsters HP -> 1")) probe::monsters_hp_1(g_msg);
    if (ImGui::Button("Save position")) probe::save_pos(g_msg);
    ImGui::SameLine();
    if (ImGui::Button("Restore position (reloads the room)")) probe::restore_pos(g_msg);

    ImGui::SeparatorText("Go to a room");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputText("room##goto", g_room, sizeof(g_room));
    ImGui::SameLine();
    ImGui::Checkbox("at x, y, z", &g_room_has_pos);
    if (g_room_has_pos) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputFloat3("##gotopos", g_room_pos, "%.2f");
    }
    ImGui::SameLine();
    if (ImGui::Button("Go##room")) probe::go_room(g_room, g_room_has_pos, g_room_pos, g_msg);
    ImGui::TextDisabled("Without a position you arrive at the room's default spawn point.");

    ImGui::SeparatorText("Flags");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("index", &g_flag_idx);
    ImGui::SameLine();
    ImGui::Text("= %d", probe::get_flag(g_flag_idx));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("new value", &g_flag_val);
    ImGui::SameLine();
    if (ImGui::Button("Set")) probe::set_flag(g_flag_idx, g_flag_val, g_msg);
    ImGui::TextDisabled("brain hold 131 = %d | boss fight 156 = %d | retry 205 = %d | double jump 163 = %d | dash 181 = %d | boost 187 = %d",
                        probe::get_flag(131), probe::get_flag(156), probe::get_flag(205), probe::get_flag(163),
                        probe::get_flag(181), probe::get_flag(187));

    ImGui::SeparatorText("Safety");
    ImGui::Checkbox("Allow saving while a test state is loaded (not recommended)", &probe::g_allow_save);
    ImGui::Checkbox("Experimental: let a test switch the character with the room load", &probe::g_allow_char_switch);
    ImGui::TextDisabled("The character switch was never tried in the game. Without it a test for another\ncharacter is greyed out until you load a save of that character.");

    ImGui::SeparatorText("Test list");
    if (ImGui::Button("Reload retail_probe_tests.ini")) {
        reload();
        g_msg = std::to_string(g_file.tests.size()) + " tests loaded.";
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d tests, %d problem(s)", (int)g_file.tests.size(), (int)g_file.errors.size());
}

void draw() {
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(660, 700), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Retail Probe", &g_open)) {
        draw_status();
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Tests")) {
                draw_tests();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Helpers")) {
                draw_helpers();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    if (!g_open) ImGui::GetIO().MouseDrawCursor = false;
}

}  // namespace panel
