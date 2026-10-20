// Diagnostic (--dl-check): did the game change a display list's inputs while the renderer was
// still reading them? At submission, walks the Fast3D list like dl_dump.cpp and records every
// RDRAM range the RSP/RDP would read (commands, matrices, vertices, MOVEMEM blocks, texture and
// TLUT loads) with a hash; after RT64 has processed the list the same ranges are hashed again.
// A changed range means the renderer may have read data from a later frame (Steam Deck ghost
// geometry, FULL_GAME.md "Linux build"). Read-only: never writes RDRAM.
#include <cstdint>
#include <vector>
#include "dl_guard.hpp"

namespace {
const uint8_t* ram = nullptr;
uint32_t seg[16]{};
uint32_t w32(uint32_t a) { return *reinterpret_cast<const uint32_t*>(ram + (a & 0x7FFFFC)); }
uint32_t phys(uint32_t a) { return (seg[(a >> 24) & 0xF] + (a & 0xFFFFFF)) & 0x7FFFFF; }

uint64_t hash_range(uint32_t addr, uint32_t len) {
    uint64_t h = 1469598103934665603ull;
    const uint32_t start = addr & ~3u, end = (addr + len + 3) & ~3u;
    for (uint32_t a = start; a < end && a < 0x800000; a += 4) { h ^= w32(a); h *= 1099511628211ull; }
    return h;
}
}

void hangtime_dl_snapshot(const uint8_t* rdram, uint32_t start, DlSnapshot& s) {
    ram = rdram;
    s.ranges.clear();
    for (auto& x : seg) x = 0;
    auto add = [&](uint32_t addr, uint32_t len, DlKind kind) {
        if (len == 0 || addr >= 0x800000) return;
        if (len > 0x800000 - addr) len = 0x800000 - addr;
        if (!s.ranges.empty()) {   // merge consecutive commands into one run
            auto& b = s.ranges.back();
            if (b.kind == kind && b.addr + b.len == addr) { b.len += len; return; }
        }
        s.ranges.push_back({addr, len, kind, 0});
    };
    std::vector<uint32_t> stack;
    uint32_t pc = start & 0x7FFFFF, timg = 0, timg_width = 0, timg_siz = 0;
    for (int count = 0; count < 60000; ++count) {
        const uint32_t a = w32(pc), b = w32(pc + 4), op = a >> 24;
        add(pc, 8, DlKind::Commands);
        pc += 8;
        switch (op) {
        case 0x01: add(phys(b), 64, DlKind::Matrix); break;                       // G_MTX
        case 0x03: add(phys(b), (a & 0xFFFF) ? (a & 0xFFFF) : 16, DlKind::Other); break;   // G_MOVEMEM
        case 0x04: add(phys(b), (((a >> 20) & 0xF) + 1) * 16, DlKind::Vertex); break;     // G_VTX
        case 0x06:                                                                 // G_DL
            if (((a >> 16) & 0xFF) == 0) stack.push_back(pc);
            pc = phys(b);
            break;
        case 0xB8:                                                                 // G_ENDDL
            if (stack.empty()) goto done;
            pc = stack.back(); stack.pop_back();
            break;
        case 0xBC:                                                                 // G_MOVEWORD segment
            if ((a & 0xFF) == 0x06) seg[(((a >> 8) & 0xFFFF) >> 2) & 0xF] = b & 0xFFFFFF;
            break;
        case 0xFD: timg = phys(b); timg_width = (a & 0xFFF) + 1; timg_siz = (a >> 19) & 3; break;   // SETTIMG
        case 0xF3: {                                                               // LOADBLOCK
            const uint32_t texels = ((b >> 12) & 0xFFF) + 1;
            add(timg, timg_siz == 0 ? (texels + 1) / 2 : texels << (timg_siz - 1), DlKind::Texture);
            break;
        }
        case 0xF4: {                                                               // LOADTILE
            const uint32_t uls = (a >> 14) & 0x3FF, ult = (a >> 2) & 0x3FF, lrs = (b >> 14) & 0x3FF, lrt = (b >> 2) & 0x3FF;
            const uint32_t bpp_x2 = 1u << timg_siz;   // bytes per texel times two
            const uint32_t row = timg_width * bpp_x2 / 2;
            if (lrt >= ult && lrs >= uls) add(timg + ult * row + uls * bpp_x2 / 2, (lrt - ult) * row + ((lrs - uls + 1) * bpp_x2 + 1) / 2, DlKind::Texture);
            break;
        }
        case 0xF0: add(timg, (((b >> 14) & 0x3FF) + 1) * 2, DlKind::Texture); break;   // LOADTLUT
        default: break;
        }
    }
done:
    for (auto& r : s.ranges) r.hash = hash_range(r.addr, r.len);
}

DlChange hangtime_dl_compare(const uint8_t* rdram, const DlSnapshot& s) {
    ram = rdram;
    DlChange c;
    for (const auto& r : s.ranges)
        if (hash_range(r.addr, r.len) != r.hash) {
            if (c.total() < 4) c.first[c.total()] = r;
            ++c.ranges[(int)r.kind]; c.bytes += r.len;
        }
    return c;
}
