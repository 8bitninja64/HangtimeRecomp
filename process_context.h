#pragma once
#include "recomp.h"
#ifdef __cplusplus
extern "C" {
#endif
int hangtime_unwinding(void);
uint32_t hangtime_claim_resume(uintptr_t);
void hangtime_forget_frame(uintptr_t);
void hangtime_note_save(uint8_t*,recomp_context*,uint32_t,uintptr_t);
void hangtime_resume_miss(recomp_context*,uint32_t);
void hangtime_context_restore(uint8_t*,recomp_context*);
void hangtime_process_enter(uint8_t*,recomp_context*);
size_t hangtime_process_count(void);
int hangtime_process_is_root(void);
#ifdef __cplusplus
}
void hangtime_process_reset();
bool hangtime_process_idle();
#endif
#define HT_FORGET_FRAME() hangtime_forget_frame((uintptr_t)&hi)
#define HT_AFTER_CALL() do { if(hangtime_unwinding()){ HT_FORGET_FRAME(); return; } } while(0)
#define HT_AFTER_CALL_RESUMABLE(CASES) do { if(hangtime_unwinding()){ \
    switch(hangtime_claim_resume((uintptr_t)&hi)){ CASES \
    case 0: HT_FORGET_FRAME(); return; \
    default: hangtime_resume_miss(ctx,0); return; } } } while(0)
