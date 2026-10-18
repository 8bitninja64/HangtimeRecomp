// The game's function partition and recompiler configuration, computed from the player's ROM on
// their machine (first-run module build), so release packages carry no function tables.
// A C++ port of full_inventory.py + prepare_full.py: verify_partition.py checks that both produce
// byte-identical full.symbols.toml and hooks.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct HangtimePartition {
    std::string symbols_toml;   // full.symbols.toml
    std::string hooks_toml;     // the [patches] hooks part of full.toml (after the [input] section)
    size_t functions = 0;
};

// `rom` is the verified big-endian (.z64) image. Returns false with `error` set if the ROM does not
// have the expected layout (it never should after the SHA-256 check).
bool hangtime_partition(const std::vector<uint8_t>& rom, HangtimePartition& out, std::string& error);
