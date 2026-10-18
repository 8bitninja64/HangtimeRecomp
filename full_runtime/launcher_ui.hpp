#pragma once
#include "launcher.hpp"
enum class LauncherResult { Play, Quit };
// Shows the pre-game launcher in its own window (SDL video/gamecontroller must be initialised).
// Edits and saves `s`; returns Play only with a verified ROM in s.rom.
LauncherResult run_launcher(LauncherSettings& s);
// First-run build of the game files from the player's ROM (module_builder.hpp), with a progress
// window. Returns false with an explanation in `error` if it failed.
bool run_module_setup(const std::filesystem::path& rom, std::string& error);
