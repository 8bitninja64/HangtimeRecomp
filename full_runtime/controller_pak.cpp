// Controller Pak (the N64 memory card in the controller), emulated at the hardware level.
//
// NBA Hangtime has no save chip in the cartridge; it keeps records and options on a Controller
// Pak. The game's own libultra code runs unchanged (osPfsIsPlug, osPfsInitPak, __osPfsGetStatus,
// __osContRamRead/__osContRamWrite and the file-system functions are recompiled originals); only
// the serial interface below them is replaced:
//   __osSiRawStartDma  copies the 64-byte PIF RAM block to/from RDRAM; on a write the PIF
//                      commands in it are executed (status, Controller Pak read/write), as the
//                      console's PIF does, and the SI interrupt message is raised
//   __osSiGetAccess / __osSiRelAccess   no-ops: every SI request here completes synchronously
// Protocol (libultra contramread.c/contramwrite.c/contpfs.c, PIF command set): a channel's
// request is [tx][rx][command and arguments][response]; 0x00 skips a channel, 0xFF is padding,
// 0xFE ends the block. Commands: 0x00/0xFF status (type 0x0005, pak bit 0x01), 0x02 read 32
// bytes at address (high 11 bits; low 5 bits are the address CRC), 0x03 write 32 bytes; reads
// and writes answer with the data CRC (__osContDataCrc), which the game verifies.
//
// One 32 KiB pak (one bank) per connected controller, stored as a raw .mpk image (the format
// emulators use) in <data folder>/saves/controller_pak_<port>.mpk. A new pak is formatted: inode
// table and backup with all pages free; the game's libultra writes the ID area itself on first use
// (__osRepairPackId), as it does for a real pak with an unreadable ID.
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>
#include "librecomp/recomp.h"
#include "ultramodern/ultramodern.hpp"
#include "launcher.hpp"

bool hangtime_controller_present(int channel);   // main.cpp: a controller is connected to this port

namespace {
constexpr size_t PAK_SIZE = 32768;
std::mutex pif_mutex;
std::array<uint8_t, 64> pif{};
struct Pak { bool loaded = false; std::vector<uint8_t> data; };
std::array<Pak, 4> paks;
std::filesystem::path pak_dir;   // empty: kept in memory only (test runs)

std::filesystem::path pak_file(int ch) { return pak_dir / ("controller_pak_" + std::to_string(ch + 1) + ".mpk"); }

// A formatted, empty 32 KiB pak: pages 1 and 2 hold the inode table and its backup with pages
// 5..127 free (0x0003); the low byte of entry 0 is their byte sum (__osPfsRWInode/__osSumcalc).
std::vector<uint8_t> formatted_pak() {
    std::vector<uint8_t> img(PAK_SIZE, 0);
    for (int p : {1, 2}) {
        uint8_t* page = &img[p * 256];
        for (int e = 5; e < 128; ++e) { page[e * 2] = 0x00; page[e * 2 + 1] = 0x03; }
        unsigned sum = 0;
        for (int b = 5 * 2; b < 256; ++b) sum += page[b];
        page[1] = (uint8_t)sum;
    }
    return img;
}

Pak& pak(int ch) {
    Pak& p = paks[ch];
    if (p.loaded) return p;
    p.loaded = true;
    if (!pak_dir.empty()) {
        std::ifstream f(pak_file(ch), std::ios::binary);
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (d.size() == PAK_SIZE) { p.data = std::move(d); fprintf(stderr, "PAK %d loaded %s\n", ch + 1, launcher_utf8(pak_file(ch)).c_str()); return p; }
    }
    p.data = formatted_pak();
    fprintf(stderr, "PAK %d new (formatted)%s\n", ch + 1, pak_dir.empty() ? ", in memory only" : "");
    return p;
}

void save(int ch) {
    if (pak_dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(pak_dir, ec);
    const auto tmp = pak_file(ch).string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(paks[ch].data.data()), PAK_SIZE);
        if (!f) { fprintf(stderr, "PAK %d: cannot write %s\n", ch + 1, tmp.c_str()); return; }
    }
    std::filesystem::rename(tmp, pak_file(ch), ec);   // replace atomically: a crash never leaves half a pak
    if (ec) { fprintf(stderr, "PAK %d: cannot save %s (%s)\n", ch + 1, launcher_utf8(pak_file(ch)).c_str(), ec.message().c_str()); return; }
    static auto last = std::chrono::steady_clock::time_point{};   // one log line per save burst
    const auto now = std::chrono::steady_clock::now();
    if (now - last > std::chrono::seconds(2)) fprintf(stderr, "PAK %d saved %s\n", ch + 1, launcher_utf8(pak_file(ch)).c_str());
    last = now;
}

// libultra __osContDataCrc: CRC-8 (polynomial 0x85) over 32 bytes plus 8 zero bits.
uint8_t data_crc(const uint8_t* data) {
    uint8_t crc = 0;
    for (int i = 0; i <= 32; ++i)
        for (int j = 7; j >= 0; --j) {
            const uint8_t x = (crc & 0x80) ? 0x85 : 0;
            crc <<= 1;
            if (i < 32) crc |= (data[i] >> j) & 1;
            crc ^= x;
        }
    return crc;
}

void command(int ch, uint8_t* cmd, int tx, uint8_t* out, int rx, uint8_t& rx_byte) {
    const bool present = ch < 4 && hangtime_controller_present(ch);
    if (!present || tx < 1) { rx_byte |= 0x80; return; }   // CHNL_ERR_NORESP: nothing on this port
    switch (cmd[0]) {
    case 0x00: case 0xFF:   // status / reset: standard controller (type 0x0005) with a pak inserted
        if (rx >= 3) { out[0] = 0x05; out[1] = 0x00; out[2] = 0x01; }
        break;
    case 0x02: {            // read 32 bytes
        if (tx < 3 || rx < 33) { rx_byte |= 0x40; return; }
        const uint32_t addr = ((cmd[1] << 8) | cmd[2]) & 0xFFE0;
        Pak& p = pak(ch);
        for (int i = 0; i < 32; ++i) out[i] = addr < PAK_SIZE ? p.data[addr + i] : 0;   // above the memory (bank/rumble registers): 0
        out[32] = data_crc(out);
        break;
    }
    case 0x03: {            // write 32 bytes
        if (tx < 35 || rx < 1) { rx_byte |= 0x40; return; }
        const uint32_t addr = ((cmd[1] << 8) | cmd[2]) & 0xFFE0;
        if (addr < PAK_SIZE) {
            Pak& p = pak(ch);
            if (!std::equal(cmd + 3, cmd + 35, p.data.begin() + addr)) { std::copy(cmd + 3, cmd + 35, p.data.begin() + addr); save(ch); }
        }
        out[0] = data_crc(cmd + 3);
        break;
    }
    default:                // controller reads go through the input layer; anything else: no answer
        rx_byte |= 0x80;
    }
}

void pif_execute() {
    int ch = 0;
    for (size_t i = 0; i < 63;) {
        const uint8_t tx = pif[i];
        if (tx == 0xFE) break;                    // end of commands
        if (tx == 0xFF) { ++i; continue; }        // padding
        if (tx == 0x00) { ++ch; ++i; continue; }  // skip this channel
        const int t = tx & 0x3F, r = pif[i + 1] & 0x3F;
        if (i + 2 + t + r > 63) break;
        command(ch, &pif[i + 2], t, &pif[i + 2 + t], r, pif[i + 1]);
        i += 2 + t + r;
        ++ch;
    }
}
}  // namespace

void hangtime_controller_pak_dir(const std::filesystem::path& dir) { pak_dir = dir; }

extern "C" void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    const int32_t direction = (int32_t)ctx->r4;   // OS_READ 0 (PIF RAM -> RDRAM), OS_WRITE 1
    {
        std::lock_guard<std::mutex> lock(pif_mutex);
        if (direction == 1) {
            for (int i = 0; i < 64; ++i) pif[i] = MEM_BU(i, ctx->r5);
            if (pif[63] & 0x01) pif_execute();   // the control byte asks the PIF to run the commands
        } else {
            for (int i = 0; i < 64; ++i) MEM_B(i, ctx->r5) = (int8_t)pif[i];
        }
    }
    ultramodern::send_si_message();   // the SI interrupt the caller waits for (osRecvMesg)
    ctx->r2 = 0;
}
extern "C" void __osSiGetAccess_recomp(uint8_t*, recomp_context*) {}
extern "C" void __osSiRelAccess_recomp(uint8_t*, recomp_context*) {}
