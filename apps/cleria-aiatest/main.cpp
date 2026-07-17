// Verification harness for the C++ AIA decoder: decode a region and write a PPM
// (+ stats) so the output can be diffed against tools/aia_decode.py.
#include "cleria/aia.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: cleria-aiatest <file.aia> [region] [out.ppm]\n"); return 2; }
    const std::uint32_t region = (argc > 2) ? std::uint32_t(std::atoi(argv[2])) : 0;
    const char* out = (argc > 3) ? argv[3] : nullptr;

    cleria::Aia a;
    if (!a.load(argv[1])) { std::fprintf(stderr, "load failed\n"); return 1; }
    std::printf("ver=%x nPal=%u nImg=%u nReg=%u canvas=%ux%u\n",
                a.version, a.nPal, a.nImg, a.nReg, a.canvasW, a.canvasH);

    cleria::AiaFrame fr;
    if (!a.decode_region(region, fr)) { std::fprintf(stderr, "decode failed\n"); return 1; }
    // --composite: also write out.ppm.composite.ppm with the second layer blended
    // over the body, so the two can be eyeballed side by side.
    const bool want_composite = argc > 4 && std::strcmp(argv[4], "--composite") == 0;
    std::size_t nz = 0;
    for (std::uint32_t i = 0; i < fr.w * fr.h; ++i)
        if (fr.rgba[i * 4 + 3] > 0) ++nz;
    std::printf("region[%u]: %ux%u  bbox_origin=(%u,%u)  non-transparent=%zu\n",
                region, fr.w, fr.h, fr.x0, fr.y0, nz);

    // Report the second layer, if this body has one.
    cleria::AiaFrame lay;
    if (a.has_layer(region) && a.decode_layer(region, lay)) {
        std::size_t lz = 0;
        for (std::uint32_t i = 0; i < lay.w * lay.h; ++i)
            if (lay.rgba[i * 4 + 3] > 0) ++lz;
        std::printf("  layer[%u]: %ux%u  bbox_origin=(%u,%u)  non-transparent=%zu\n",
                    region + 1, lay.w, lay.h, lay.x0, lay.y0, lz);
    } else {
        std::printf("  layer: none (body flags bit0 clear)\n");
    }

    auto write_ppm = [](const std::string& path, const cleria::AiaFrame& f0) {
        std::ofstream f(path, std::ios::binary);
        f << "P6\n" << f0.w << " " << f0.h << "\n255\n";
        for (std::uint32_t i = 0; i < f0.w * f0.h; ++i) {
            const std::uint8_t* p = &f0.rgba[i * 4];
            const int a8 = p[3];
            for (int c = 0; c < 3; ++c) f.put(char((p[c] * a8) / 255));
        }
        std::printf("wrote %s (%ux%u)\n", path.c_str(), f0.w, f0.h);
    };
    if (out) {  // PPM: composite over black so it's directly viewable/diffable.
        write_ppm(out, fr);
        if (want_composite && a.has_layer(region) && lay.w) {
            // Write BOTH blends. They are the argument: on C_115 `over` erases the
            // body (which is what made us wrongly abandon the overlay reading) while
            // `add` glows. `add` is the engine's — SRCALPHA/ONE, read out of
            // FUN_004745a0.
            cleria::AiaFrame over = fr;
            cleria::composite_over(over, lay);
            write_ppm(std::string(out) + ".over.ppm", over);
            cleria::AiaFrame add = fr;
            cleria::composite_add(add, lay);
            write_ppm(std::string(out) + ".add.ppm", add);
        }
    }
    return 0;
}
