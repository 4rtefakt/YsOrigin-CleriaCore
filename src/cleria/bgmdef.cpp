#include "cleria/bgmdef.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace cleria {
namespace fs = std::filesystem;

bool BgmDefs::load(const fs::path& asset_root, bool japanese) {
    std::vector<fs::path> cands;
    if (const char* e = std::getenv("CLERIA_MUSIC_DIR")) cands.emplace_back(e);
    cands.push_back(asset_root / "music");
    cands.push_back(asset_root.parent_path() / "music");
    cands.push_back(asset_root.parent_path() / "release" / "music");

    const char* file = japanese ? "bgmdef2.txt" : "bgmdef.txt";
    for (const fs::path& d : cands) {
        std::error_code ec;
        if (d.empty() || !fs::exists(d / file, ec)) continue;
        std::ifstream f(d / file);
        if (!f) continue;
        std::string line;
        while (std::getline(f, line)) {
            // ';' comments out the rest of the line (the EFFECTDEF convention).
            const std::size_t c = line.find(';');
            if (c != std::string::npos) line.resize(c);
            std::istringstream ls(line);
            int id = 0;
            std::string name;
            if (ls >> id >> name && !name.empty()) names_[id] = name;
        }
        if (!names_.empty()) {
            dir_ = d;
            return true;
        }
    }
    return false;
}

std::string BgmDefs::name(int id) const {
    const auto it = names_.find(id);
    return it == names_.end() ? std::string() : it->second;
}

fs::path BgmDefs::track(int id) const {
    const std::string n = name(id);
    if (n.empty() || dir_.empty()) return {};
    const fs::path p = dir_ / (n + ".ogg");
    std::error_code ec;
    return fs::exists(p, ec) ? p : fs::path();
}

}  // namespace cleria
