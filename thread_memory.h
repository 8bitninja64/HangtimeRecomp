#include "process_context.h"
#include "recomp.h"
#ifdef __cplusplus
extern "C" {
#endif
void hangtime_safe_point(uint8_t*, recomp_context*, uint32_t);
void hangtime_startup_resume_probe(uint8_t*, recomp_context*);
void hangtime_entry_gate(uint8_t*, recomp_context*, uint32_t, const char*, int);
int32_t* thread_word(uint8_t*, uint32_t);
int16_t* thread_half(uint8_t*, uint32_t);
int8_t* thread_byte(uint8_t*, uint32_t);
#ifdef __cplusplus
}
#endif
#undef MEM_W
#define MEM_W(offset, reg) (*thread_word(rdram, (uint32_t)((reg)+(offset))))
#undef MEM_H
#undef MEM_HU
#undef MEM_B
#undef MEM_BU
#define MEM_H(offset, reg) (*thread_half(rdram, (uint32_t)((reg)+(offset))))
#define MEM_HU(offset, reg) ((uint16_t)MEM_H(offset,reg))
#define MEM_B(offset, reg) (*thread_byte(rdram, (uint32_t)((reg)+(offset))))
#define MEM_BU(offset, reg) ((uint8_t)MEM_B(offset,reg))
