// Diagnostic: decode one frame's Fast3D (F3D) display list from RDRAM into text (--dump-dl).
// Read-only. Follows the segment table (G_MOVEWORD G_MW_SEGMENT) and G_DL calls/branches.
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
const uint8_t* ram = nullptr;
uint32_t seg[16]{};
uint32_t w32(uint32_t a) { return *reinterpret_cast<const uint32_t*>(ram + (a & 0xFFFFFC)); }
int16_t s16(uint32_t a) { return *reinterpret_cast<const int16_t*>(ram + ((a & 0xFFFFFE) ^ 2)); }
uint16_t u16(uint32_t a) { return *reinterpret_cast<const uint16_t*>(ram + ((a & 0xFFFFFE) ^ 2)); }
uint8_t u8(uint32_t a) { return ram[(a & 0xFFFFFF) ^ 3]; }
uint32_t phys(uint32_t a) { return (seg[(a >> 24) & 0xF] + (a & 0xFFFFFF)) & 0x7FFFFF; }
const char* fmt_name(uint32_t f) { static const char* n[] = {"RGBA", "YUV", "CI", "IA", "I", "?", "?", "?"}; return n[f & 7]; }
}

void hangtime_dump_dl(const uint8_t* rdram, uint32_t start, FILE* out) {
    ram = rdram;
    for (auto& s : seg) s = 0;
    std::vector<uint32_t> stack;
    uint32_t pc = start & 0xFFFFFF, half1 = 0, half2 = 0;
    int count = 0;
    while (count++ < 60000) {
        const uint32_t a = w32(pc), b = w32(pc + 4), op = a >> 24;
        const int depth = (int)stack.size();
        auto line = [&](const char* fmt, auto... args) {
            fprintf(out, "%06X %*s", pc, depth * 2, "");
            fprintf(out, fmt, args...);
            fputc('\n', out);
        };
        pc += 8;
        switch (op) {
        case 0x01: {  // G_MTX
            const uint32_t p = (a >> 16) & 0xFF, m = phys(b);
            line("MTX %s %s %s @%06X", p & 1 ? "PROJ" : "MODELVIEW", p & 2 ? "LOAD" : "MUL", p & 4 ? "PUSH" : "NOPUSH", m);
            for (int r = 0; r < 4; ++r) {
                fprintf(out, "%06X %*s    ", pc - 8, depth * 2, "");
                for (int c = 0; c < 4; ++c) {
                    const int i = r * 4 + c;
                    fprintf(out, "%11.4f", s16(m + 2 * i) + u16(m + 32 + 2 * i) / 65536.0);
                }
                fputc('\n', out);
            }
            break;
        }
        case 0x03: {  // G_MOVEMEM
            const uint32_t idx = (a >> 16) & 0xFF, m = phys(b);
            if (idx == 0x80) line("VIEWPORT scale %.2f %.2f %.2f trans %.2f %.2f %.2f", s16(m) / 4.0, s16(m + 2) / 4.0, s16(m + 4) / 4.0,
                                  s16(m + 8) / 4.0, s16(m + 10) / 4.0, s16(m + 12) / 4.0);
            else line("MOVEMEM idx=%02X len=%u @%06X", idx, a & 0xFFFF, m);
            break;
        }
        case 0x04: {  // G_VTX
            const uint32_t n = ((a >> 20) & 0xF) + 1, v0 = (a >> 16) & 0xF, m = phys(b);
            line("VTX n=%u v0=%u @%06X", n, v0, m);
            for (uint32_t i = 0; i < n; ++i) {
                const uint32_t v = m + i * 16;
                fprintf(out, "%06X %*s    v%-2u xyz %6d %6d %6d  st %6d %6d  rgba %02X%02X%02X%02X\n", pc - 8, depth * 2, "", v0 + i,
                        s16(v), s16(v + 2), s16(v + 4), s16(v + 8), s16(v + 10), u8(v + 12), u8(v + 13), u8(v + 14), u8(v + 15));
            }
            break;
        }
        case 0x06: {  // G_DL
            const bool branch = ((a >> 16) & 0xFF) != 0;
            line("DL %s %06X", branch ? "BRANCH" : "CALL", phys(b));
            if (!branch) stack.push_back(pc);
            pc = phys(b);
            break;
        }
        case 0xB8:  // G_ENDDL
            line("ENDDL");
            if (stack.empty()) return;
            pc = stack.back(); stack.pop_back();
            break;
        case 0xBF: line("TRI1 %u %u %u", ((b >> 16) & 0xFF) / 10, ((b >> 8) & 0xFF) / 10, (b & 0xFF) / 10); break;
        case 0xBC: {  // G_MOVEWORD
            const uint32_t idx = a & 0xFF, off = (a >> 8) & 0xFFFF;
            if (idx == 0x06) { seg[(off >> 2) & 0xF] = b & 0xFFFFFF; line("SEGMENT %u = %06X", (off >> 2) & 0xF, b & 0xFFFFFF); }
            else line("MOVEWORD idx=%02X off=%04X val=%08X", idx, off, b);
            break;
        }
        case 0xBD: line("POPMTX"); break;
        case 0xBE: line("CULLDL %u %u", (a & 0xFFFF) / 40, (b & 0xFFFF) / 40); break;
        case 0xBB: line("TEXTURE s=%04X t=%04X tile=%u %s", b >> 16, b & 0xFFFF, (a >> 8) & 7, (a & 0xFF) ? "on" : "off"); break;
        case 0xBA: line("OTHERMODE_H shift=%u len=%u val=%08X", (a >> 8) & 0xFF, a & 0xFF, b); break;
        case 0xB9: line("OTHERMODE_L shift=%u len=%u val=%08X", (a >> 8) & 0xFF, a & 0xFF, b); break;
        case 0xB7: line("SETGEOMETRY %08X", b); break;
        case 0xB6: line("CLEARGEOMETRY %08X", b); break;
        case 0xB4: half1 = b; break;                         // G_RDPHALF_1
        case 0xB3: half2 = b; line("  texrect st s=%.2f t=%.2f", (int16_t)(b >> 16) / 32.0, (int16_t)(b & 0xFFFF) / 32.0); break;  // G_RDPHALF_2
        case 0xB2: line("  texrect d dsdx=%.3f dtdy=%.3f", (int16_t)(b >> 16) / 1024.0, (int16_t)(b & 0xFFFF) / 1024.0); break;  // G_RDPHALF_CONT
        case 0xE4: case 0xE5:
            line("%s tile=%u x %.2f..%.2f y %.2f..%.2f", op == 0xE4 ? "TEXRECT" : "TEXRECTFLIP", (b >> 24) & 7,
                 ((b >> 12) & 0xFFF) / 4.0, ((a >> 12) & 0xFFF) / 4.0, (b & 0xFFF) / 4.0, (a & 0xFFF) / 4.0);
            break;
        case 0xF6: line("FILLRECT x %u..%u y %u..%u", (b >> 14) & 0x3FF, (a >> 14) & 0x3FF, (b >> 2) & 0x3FF, (a >> 2) & 0x3FF); break;
        case 0xED: line("SCISSOR x %.2f..%.2f y %.2f..%.2f mode %u", ((a >> 12) & 0xFFF) / 4.0, ((b >> 12) & 0xFFF) / 4.0,
                        (a & 0xFFF) / 4.0, (b & 0xFFF) / 4.0, (b >> 24) & 3); break;
        case 0xFD: line("SETTIMG %s %ub w=%u @%06X", fmt_name(a >> 21), 4u << ((a >> 19) & 3), (a & 0xFFF) + 1, phys(b)); break;
        case 0xFF: line("SETCIMG %s w=%u @%06X", fmt_name(a >> 21), (a & 0xFFF) + 1, phys(b)); break;
        case 0xFE: line("SETZIMG @%06X", phys(b)); break;
        case 0xF5: line("SETTILE tile=%u fmt=%s line=%u tmem=%u pal=%u", (b >> 24) & 7, fmt_name(a >> 21), (a >> 9) & 0x1FF, a & 0x1FF, (b >> 20) & 0xF); break;
        case 0xF2: line("SETTILESIZE tile=%u %.2f,%.2f..%.2f,%.2f", (b >> 24) & 7, ((a >> 12) & 0xFFF) / 4.0, (a & 0xFFF) / 4.0,
                        ((b >> 12) & 0xFFF) / 4.0, (b & 0xFFF) / 4.0); break;
        case 0xF3: line("LOADBLOCK tile=%u texels=%u", (b >> 24) & 7, ((b >> 12) & 0xFFF) + 1); break;
        case 0xF4: line("LOADTILE tile=%u", (b >> 24) & 7); break;
        case 0xF0: line("LOADTLUT tile=%u", (b >> 24) & 7); break;
        case 0xFC: line("SETCOMBINE %08X %08X", a, b); break;
        case 0xF7: line("SETFILLCOLOR %08X", b); break;
        case 0xF8: line("SETFOG %08X", b); break;
        case 0xF9: line("SETBLEND %08X", b); break;
        case 0xFA: line("SETPRIM %08X", b); break;
        case 0xFB: line("SETENV %08X", b); break;
        case 0xE6: case 0xE7: case 0xE8: break;              // syncs
        case 0xE9: line("FULLSYNC"); break;
        case 0x00: line(a == 0x00525464 ? "RT64 HOOK %08X" : "NOOP %08X", b); break;
        case 0x64: {  // RT64 extended GBI (widescreen.cpp output)
            const uint32_t sub = a & 0xFFFFFF, c = w32(pc), d = w32(pc + 4), e = w32(pc + 8), g = w32(pc + 12);
            if (sub == 0x02) {
                line("EX_TEXRECT tile=%u%s x %.2f..%.2f y %.2f..%.2f s=%.2f t=%.2f dsdx=%.3f", b & 7, (b >> 7) & 1 ? " flip" : "",
                     (int16_t)(c >> 16) / 4.0, (int16_t)(d >> 16) / 4.0, (int16_t)c / 4.0, (int16_t)d / 4.0, (int16_t)(e >> 16) / 32.0, (int16_t)e / 32.0, (int16_t)(g >> 16) / 1024.0);
                pc += 16;
            }
            else if (sub == 0x03 || sub == 0x05) {
                line("%s origins %03X/%03X x %.2f..%.2f y %.2f..%.2f", sub == 0x03 ? "EX_FILLRECT" : "EX_SCISSOR",
                     sub == 0x03 ? b & 0xFFF : (b >> 2) & 0xFFF, sub == 0x03 ? (b >> 12) & 0xFFF : (b >> 14) & 0xFFF,
                     (int16_t)(c >> 16) / 4.0, (int16_t)(d >> 16) / 4.0, (int16_t)c / 4.0, (int16_t)d / 4.0);
                pc += 8;
            }
            else line("EX op %06X %08X", sub, b);
            break;
        }
        default: line("op %02X %08X %08X", op, a, b); break;
        }
    }
    fprintf(out, "(stopped after 60000 commands)\n");
}
