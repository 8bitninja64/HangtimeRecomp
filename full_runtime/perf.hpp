#pragma once
// --perf PATH: high-resolution event log for frame-timing analysis (perf_report.py).
// One CSV line per event: kind,t_us,a,b (t_us = microseconds since perf_open, steady clock).
// Kinds: V VI (a = VI count), P game input poll (a = poll count), D/d display list begin/end
// (a = index), S/s screen update begin/end (a = VI origin presented, b = VI index), M mode word
// change (a = mode), R/r RT64 profiler averages over their last 120 samples (microseconds; see
// rt64_context.cpp), h RT64 profiler ring positions (present, workload). Diagnostic only: no
// effect on the game when --perf is absent.
//
// --profile-gfx PATH (Windows): samples the gfx thread's call stack about every millisecond
// while it is inside a display list (phase 1) or a screen update (phase 2); profile_report.py
// resolves the stacks with the linker map and lists where long calls spend their time.
#include <cstdint>

void perf_open(const char* path);
bool perf_enabled();
void perf_event(char kind, uint64_t a = 0, uint64_t b = 0);
void perf_flush();

void perf_profile_open(const char* path);
void perf_gfx_phase(int phase);   // called on the gfx thread: 1 display list, 2 screen update, 0 neither
void perf_profile_flush();
