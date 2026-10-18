#pragma once
// Whole-program build shim for process_context.cpp: standard recomp.h RDRAM access (the harness's
// thread_memory.h routes MEM_* through bounds-checked fixture accessors instead).
#include <recomp.h>
#include "process_context.h"
