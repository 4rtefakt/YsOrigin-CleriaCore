// Item id -> display name, from MISC/INVINFO.DAT (128 records x 184 bytes; the
// record index is the item id; the English name is at record offset 0, NUL-
// terminated ASCII). English names come from the data_us archive.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace cleria {

struct ItemTable {
    std::vector<std::string> names;  // indexed by item id

    static ItemTable load(const std::filesystem::path& invinfo_dat);
    std::string name(int id) const;  // falls back to "item 0xNN" if unknown
};

}  // namespace cleria
