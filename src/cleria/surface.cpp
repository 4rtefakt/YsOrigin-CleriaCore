#include "cleria/surface.hpp"

#include <cctype>
#include <cstring>

namespace cleria {

int surface_index(std::string_view name) {
    for (int i = 0; i < kSurfaceCount; ++i) {
        const char* t = kSurfaceTable[i].name;
        const std::size_t n = std::strlen(t);
        if (n != name.size()) continue;
        bool eq = true;
        for (std::size_t k = 0; k < n; ++k)
            if (std::tolower((unsigned char)t[k]) != std::tolower((unsigned char)name[k])) {
                eq = false;
                break;
            }
        if (eq) return i;
    }
    return -1;
}

int footstep_sound(int surface, unsigned step_n) {
    if (surface < 0 || surface >= kSurfaceCount) return -1;
    const SurfaceType& s = kSurfaceTable[surface];
    if (s.base_sound_id == 0) return -1;  // 'none' / 'stopper' / 'reserve20'
    return int(s.base_sound_id) + int(step_n & s.variant_mask);
}

}  // namespace cleria
