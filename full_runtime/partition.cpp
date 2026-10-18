// Port of full_inventory.py (function partition) and prepare_full.py (symbols and hooks); see
// partition.hpp. Keep the two in step: verify_partition.py compares their outputs byte for byte.
#include "partition.hpp"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <utility>

namespace {

constexpr uint32_t TEXT_START = 0x80000400, TEXT_END = 0x800780B0, DATA_END = 0x800A3880;
constexpr uint32_t JR_RA = 0x03E00008;
constexpr uint32_t SAVE = 0x80068C40, RESTORE = 0x80068C8C, ENTER = 0x80068D0C, DISPATCH = 0x8005B770;

const std::map<uint32_t, const char*> LIBULTRA = {
#include "libultra_names.inc"
};

struct Rom {
    const std::vector<uint8_t>& d;
    uint32_t word(uint32_t pc) const {
        const size_t o = size_t(pc - 0x80000400u) + 0x1000;
        if (o + 4 > d.size()) return 0;
        return (uint32_t(d[o]) << 24) | (uint32_t(d[o + 1]) << 16) | (uint32_t(d[o + 2]) << 8) | d[o + 3];
    }
};

int32_t simm(uint32_t w) { return int16_t(w & 0xFFFF); }

// review_function.branch_target: conditional branches (incl. likely, REGIMM, COP1).
bool branch_target(uint32_t pc, uint32_t w, uint32_t& t) {
    const uint32_t op = w >> 26, rt = (w >> 16) & 31, rs = (w >> 21) & 31;
    const bool b = (op >= 4 && op <= 7) || (op >= 20 && op <= 23) ||
                   (op == 1 && (rt <= 3 || (rt >= 16 && rt <= 19))) || (op == 17 && rs == 8);
    if (b) t = pc + 4 + uint32_t(simm(w) * 4);
    return b;
}
uint32_t jal_target(uint32_t pc, uint32_t w) { return ((pc + 4) & 0xF0000000u) | ((w & 0x3FFFFFFu) << 2); }

uint32_t natural_end(const Rom& r, uint32_t start, uint32_t limit) {
    uint32_t reach = start;
    for (uint32_t pc = start; pc < limit; pc += 4) {
        const uint32_t w = r.word(pc), op = w >> 26;
        uint32_t t;
        if (branch_target(pc, w, t) && t > reach) reach = t;
        if (w == JR_RA && pc >= reach) return pc + 8;
        if (op == 2 && pc >= reach) {
            const uint32_t tgt = jal_target(pc, w);
            if (!(start <= tgt && tgt < pc + 8)) return pc + 8;
        }
        if (w == 0x1000FFFF && pc >= reach) return pc + 8;
    }
    return limit;
}

std::string hex(uint32_t v, bool pad8) {
    char b[16];
    snprintf(b, sizeof b, pad8 ? "%08X" : "%X", v);
    return b;
}

}  // namespace

bool hangtime_partition(const std::vector<uint8_t>& rom, HangtimePartition& out, std::string& error) {
    const Rom r{rom};
    if (rom.size() < 0x1000 + (DATA_END - 0x80000400)) { error = "ROM too small"; return false; }
    auto plausible_start = [&](uint32_t a) { return TEXT_START <= a && a < TEXT_END && a % 4 == 0 && r.word(a) != 0; };
    auto follows_return = [&](uint32_t a) {
        if (a == TEXT_START) return true;
        uint32_t k = a - 4;
        while (k > a - 64 && r.word(k) == 0) k -= 4;
        return r.word(k) == JR_RA || r.word(k - 4) == JR_RA;
    };

    // --- full_inventory.py ---
    std::set<uint32_t> starts{TEXT_START};
    for (uint32_t pc = TEXT_START; pc < TEXT_END; pc += 4) {
        const uint32_t w = r.word(pc);
        if (w >> 26 == 3) { const uint32_t t = jal_target(pc, w); if (TEXT_START <= t && t < TEXT_END) starts.insert(t); }
    }
    std::set<uint32_t> table_words;   // switch jump tables: lui rY,HI / lw rX,LO(rY) / jr rX
    for (uint32_t pc = TEXT_START; pc < TEXT_END; pc += 4) {
        const uint32_t w = r.word(pc);
        if (!(w >> 26 == 0 && (w & 63) == 8 && ((w >> 21) & 31) != 31)) continue;
        const uint32_t reg = (w >> 21) & 31;
        bool have = false; uint32_t base = 0;
        for (uint32_t k = pc - 4; k > pc - 48; k -= 4) {
            const uint32_t x = r.word(k);
            if (x >> 26 == 35 && ((x >> 16) & 31) == reg) {
                const uint32_t ry = (x >> 21) & 31; const int32_t lo = simm(x);
                for (uint32_t m = k - 4; m > k - 48; m -= 4) {
                    const uint32_t y = r.word(m);
                    if (y >> 26 == 15 && ((y >> 16) & 31) == ry) { base = ((y & 0xFFFF) << 16) + uint32_t(lo); have = true; break; }
                }
                break;
            }
        }
        if (!have || !(TEXT_END <= base && base < DATA_END)) continue;
        for (uint32_t a = base; a < DATA_END; a += 4) {
            const uint32_t v = r.word(a);
            if (!(TEXT_START <= v && v < TEXT_END) || !(int64_t(v) - int64_t(pc) < 0x4000 && int64_t(pc) - int64_t(v) < 0x4000)) break;
            table_words.insert(a);
        }
    }
    std::set<uint32_t> found;   // code pointers in the data image and built with lui/addiu
    for (uint32_t a = TEXT_END; a < DATA_END; a += 4) {
        if (table_words.count(a)) continue;
        const uint32_t v = r.word(a);
        if (plausible_start(v) && (starts.count(v) || follows_return(v))) found.insert(v);
    }
    std::map<uint32_t, uint32_t> his;
    for (uint32_t pc = TEXT_START; pc < TEXT_END; pc += 4) {
        const uint32_t w = r.word(pc), op = w >> 26, rt = (w >> 16) & 31, rs = (w >> 21) & 31;
        if (op == 15) { his[rt] = (w & 0xFFFF) << 16; continue; }
        if (op == 9 && his.count(rs)) {
            const uint32_t v = his[rs] + uint32_t(simm(w));
            if (plausible_start(v) && (starts.count(v) || follows_return(v))) found.insert(v);
        }
    }
    starts.insert(found.begin(), found.end());
    for (bool changed = true; changed;) {   // code after a function's natural end starts a function
        changed = false;
        const std::vector<uint32_t> ordered(starts.begin(), starts.end());
        for (size_t i = 0; i < ordered.size(); ++i) {
            const uint32_t s = ordered[i], limit = i + 1 < ordered.size() ? ordered[i + 1] : TEXT_END;
            uint32_t pc = natural_end(r, s, limit);
            while (pc < limit && r.word(pc) == 0) pc += 4;
            if (pc < limit && !starts.count(pc)) { starts.insert(pc); changed = true; }
        }
    }
    std::vector<std::pair<uint32_t, uint32_t>> funcs;
    {
        const std::vector<uint32_t> ordered(starts.begin(), starts.end());
        for (size_t i = 0; i < ordered.size(); ++i) funcs.push_back({ordered[i], i + 1 < ordered.size() ? ordered[i + 1] : TEXT_END});
    }

    // --- prepare_full.py ---
    for (const auto& [a, n] : LIBULTRA)
        if (!starts.count(a)) { error = "libultra name on a non-function address " + hex(a, true); return false; }
    auto name = [&](uint32_t a) -> std::string {
        if (a == RESTORE) return "main_context_restore";
        if (a == ENTER) return "main_stack_enter";
        auto it = LIBULTRA.find(a);
        return it != LIBULTRA.end() ? it->second : "func_" + hex(a, true);
    };
    auto func_of = [&](uint32_t pc) {
        return std::prev(std::upper_bound(funcs.begin(), funcs.end(), std::make_pair(pc, UINT32_MAX)))->first;
    };
    std::string sym = "[[section]]\nname=\"hangtime_image\"\nrom=0x1000\nvram=0x80000400\nsize=0xA3480\nfunctions=[\n";
    for (size_t i = 0; i < funcs.size(); ++i)
        sym += "{name=\"" + name(funcs[i].first) + "\",vram=0x" + hex(funcs[i].first, false) + ",size=0x" +
               hex(funcs[i].second - funcs[i].first, false) + "}" + (i + 1 < funcs.size() ? ",\n" : "\n");
    sym += "]\n";

    // Hooks in Python dict insertion order: key (function name, pc) -> texts.
    std::vector<std::pair<std::string, uint32_t>> keys;
    std::map<std::pair<std::string, uint32_t>, std::vector<std::string>> hooks;
    auto slot = [&](const std::string& fn, uint32_t pc) -> std::vector<std::string>& {
        auto key = std::make_pair(fn, pc);
        if (!hooks.count(key)) keys.push_back(key);
        return hooks[key];
    };
    auto is_jalr = [](uint32_t w) { return (w >> 26) == 0 && (w & 63) == 9; };
    auto is_jump_or_branch = [&](uint32_t w) {
        uint32_t t; const uint32_t op = w >> 26;
        return branch_target(0, w, t) || op == 2 || op == 3 || (op == 0 && ((w & 63) == 8 || (w & 63) == 9));
    };
    for (const auto& [s, e] : funcs) {
        if (LIBULTRA.count(s) || s == RESTORE || s == ENTER) continue;
        const std::string fn = name(s);
        std::string cases;
        for (uint32_t pc = s; pc < e; pc += 4)
            if (r.word(pc) >> 26 == 3 && jal_target(pc, r.word(pc)) == SAVE) cases += "case 0x" + hex(pc + 8, true) + "u: goto R_" + hex(pc + 8, true) + "; ";
        for (uint32_t pc = s; pc < e; pc += 4) {
            const uint32_t w = r.word(pc), op = w >> 26;
            uint32_t t;
            if (branch_target(pc, w, t) && s <= t && t <= pc) slot(fn, pc).push_back("hangtime_safe_point(rdram, ctx, 0x" + hex(pc, false) + "u);");
            if (w == JR_RA) slot(fn, pc).push_back("HT_FORGET_FRAME();");
            if (!(op == 3 || is_jalr(w))) continue;
            if (pc != s && is_jump_or_branch(r.word(pc - 4))) { error = "call in a delay slot at " + hex(pc, true); return false; }
            if (pc + 8 >= e) continue;
            if (op == 3 && jal_target(pc, w) == SAVE) {
                slot(fn, pc).push_back("ctx->r31 = (int64_t)(int32_t)0x" + hex(pc + 8, false) + ";");
                slot(fn, pc + 8).push_back("hangtime_note_save(rdram, ctx, 0x" + hex(pc + 8, true) + "u, (uintptr_t)&hi); R_" + hex(pc + 8, true) + ": ;");
                continue;
            }
            slot(fn, pc + 8).push_back(cases.empty() ? "HT_AFTER_CALL();" : "HT_AFTER_CALL_RESUMABLE(" + cases + ");");
        }
    }
    auto first = [&](const std::string& fn, uint32_t pc, const std::string& text) { auto& v = slot(fn, pc); v.insert(v.begin(), text); };
    // Widescreen (widescreen.cpp): frame start, floor, the sprite dispatcher's entry and return.
    const std::pair<uint32_t, const char*> ws[] = {{0x8005643C, "hangtime_ws_frame_begin(rdram, ctx);"}, {0x80061594, "hangtime_ws_floor(rdram, ctx);"},
                                                   {0x8005B770, "hangtime_ws_sprite_begin(rdram, ctx);"}, {0x8005C140, "hangtime_ws_sprite_end(rdram, ctx);"}};
    if (r.word(0x8005C140) != JR_RA || func_of(0x8005C140) != DISPATCH || func_of(DISPATCH) != DISPATCH) { error = "unexpected sprite dispatcher layout"; return false; }
    for (const auto& [pc, text] : ws) first(name(func_of(pc)), pc, text);
    for (uint32_t pc : {0x8005FFACu, 0x800606D4u, 0x80061BECu, 0x8006243Cu})
        first(name(pc), pc, "hangtime_ws_object(rdram, ctx->r4, 0x" + hex(pc, true) + "u);");
    // The player draw routine's whole-player screen cull (prepare_full.py comments).
    const std::pair<uint32_t, uint32_t> cull_words[] = {{0x80060AC8, 0x8DD90000}, {0x80060AD0, 0x07200269}, {0x80060AD8, 0x8F0F0000},
                                                        {0x80060AE0, 0x29E10140}, {0x80060AE4, 0x10200264}, {0x80061478, 0x27BD00A0}};
    for (const auto& [pc, w] : cull_words) if (r.word(pc) != w) { error = "unexpected player cull code at " + hex(pc, true); return false; }
    if (!std::count(funcs.begin(), funcs.end(), std::make_pair(0x800606D4u, 0x80061484u))) { error = "unexpected player draw range"; return false; }
    for (const auto& [pc, step] : {std::make_pair(0x80060AD0u, 0), std::make_pair(0x80060AE0u, 1), std::make_pair(0x80060AECu, 2), std::make_pair(0x80061478u, 2)})
        first(name(0x800606D4), pc, "hangtime_ws_player_cull(rdram, ctx, " + std::to_string(step) + ");");
    first(name(0x80048E60), 0x80048E60, "hangtime_cheats_matchup(rdram, ctx);");   // cheats.cpp
    int dispatch_calls = 0;
    for (const auto& [s, e] : funcs)
        for (uint32_t pc = s; pc < e; pc += 4)
            if (r.word(pc) >> 26 == 3 && jal_target(pc, r.word(pc)) == DISPATCH) {
                first(name(s), pc, "hangtime_ws_caller(0x" + hex(pc, true) + "u);");
                ++dispatch_calls;
            }
    if (dispatch_calls != 12) { error = "unexpected sprite dispatcher call count"; return false; }

    std::string text;
    for (size_t i = 0; i < keys.size(); ++i) {
        auto texts = hooks[keys[i]];
        std::stable_sort(texts.begin(), texts.end(), [](const std::string& a, const std::string& b) {
            return a.rfind("hangtime_safe_point", 0) != 0 && b.rfind("hangtime_safe_point", 0) == 0;
        });
        std::string body;
        for (size_t j = 0; j < texts.size(); ++j) body += (j ? " " : "") + texts[j];
        std::string esc;
        for (char c : body) { if (c == '\\' || c == '"') esc += '\\'; esc += c; }
        if (i) text += "\n";
        text += "[[patches.hook]]\nfunc=\"" + keys[i].first + "\"\nbefore_vram=0x" + hex(keys[i].second, false) + "\ntext=\"" + esc + "\"\n";
    }
    out.symbols_toml = sym;
    out.hooks_toml = "[patches]\nignored=[\"main_context_restore\",\"main_stack_enter\"]\n\n" + text;
    out.functions = funcs.size();
    return true;
}
