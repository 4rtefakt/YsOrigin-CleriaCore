// Minimal DDS loader for Ys Origin textures: DXT1 (BC1), DXT3 (BC2), and
// uncompressed 32-bit BGRA. Returns mip 0 only. Format codes are the WebGPU
// WGPUTextureFormat integer values so cleria core stays GPU-API-agnostic.
#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace cleria {

struct Dds {
    std::uint32_t width = 0, height = 0;
    // WGPUTextureFormat values: BC1RGBAUnorm=... we store a small enum instead.
    enum Format { None, BC1, BC2, RGBA8 } format = None;
    std::vector<std::uint8_t> data;  // mip 0, as RGBA8 or BC block bytes
    bool ok() const { return format != None && !data.empty(); }
};

// Load mip 0 of a DDS file. Returns {None} on unsupported/failed.
Dds load_dds(const std::filesystem::path& path);

}  // namespace cleria
