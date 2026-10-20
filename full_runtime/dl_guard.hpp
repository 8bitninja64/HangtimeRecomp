#pragma once
// --dl-check: display-list input snapshot / compare (dl_guard.cpp).
#include <cstdint>
#include <vector>

enum class DlKind : uint8_t { Commands, Matrix, Vertex, Texture, Other, Count };
struct DlRange { uint32_t addr, len; DlKind kind; uint64_t hash; };
struct DlSnapshot { std::vector<DlRange> ranges; };
struct DlChange {
    unsigned ranges[(int)DlKind::Count]{};
    uint64_t bytes = 0;
    DlRange first[4]{};   // the first changed ranges (address, length, kind)
    unsigned total() const { unsigned t = 0; for (unsigned r : ranges) t += r; return t; }
};

void hangtime_dl_snapshot(const uint8_t* rdram, uint32_t start, DlSnapshot& s);
DlChange hangtime_dl_compare(const uint8_t* rdram, const DlSnapshot& s);
