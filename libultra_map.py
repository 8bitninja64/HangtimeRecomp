"""libultra identification for the whole-program build (address -> name, with evidence).

Names matter because N64Recomp maps the libultra names in its symbol lists to N64ModernRuntime
reimplementations (reimplemented) or skips them (ignored). Evidence keys:
  H  behaviour verified by the native harness (its hook/range replaced or ran this routine)
  S  call structure: callee set/order matches libultra 2.0 source for this function
  M  MMIO block / COP0 registers touched match the function's hardware role
Confidence: 'high' for H or S+M; 'medium' for structure-only inferences (controller pak).
"""

LIBULTRA = {
    # --- threads, messages, events (H: harness hooks; S: Disable/Enqueue/Restore pattern)
    0x8006A8C0: ('osCreateThread', 'H event_create; S Disable/Restore', 'high'),
    0x8006AA10: ('osStartThread', 'H event_start; S EnqueueThread/Dispatch', 'high'),
    0x8006AB60: ('osSetThreadPri', 'S Disable,__osDequeueThread,__osEnqueueThread,EnqueueAndYield', 'high'),
    0x80073F40: ('osGetThreadPri', 'S 0x20 leaf called by both managers before osSetThreadPri', 'high'),
    0x80076020: ('osDestroyThread', 'S Disable,__osDequeueThread,__osDispatchThread,Restore', 'high'),
    0x80069060: ('osCreateMesgQueue', 'H audio_queue_create; 0x30 leaf, 10 callers', 'high'),
    0x80068F20: ('osRecvMesg', 'H graphics_receive; S EnqueueAndYield/PopThread/osStartThread', 'high'),
    0x8006EC00: ('osSendMesg', 'S same shape as osRecvMesg, 8 callers incl. osPiStartDma', 'high'),
    0x8006EA80: ('osJamMesg', 'S osPiStartDma high-priority branch, same shape as osSendMesg', 'high'),
    0x8006AC40: ('osSetEventMesg', 'H event_register', 'high'),
    0x8006ACB0: ('osViSetEvent', 'H event_vi_register', 'high'),
    0x8006BA30: ('osGetTime', 'S Disable,osGetCount,Restore', 'high'),
    0x80074A80: ('osSetTimer', 'S __osInsertTimer,__osSetTimerIntr', 'high'),
    0x800742D0: ('osGetCount', 'M mfc0 Count', 'high'),
    0x8006B420: ('osSetIntMask', 'H audio_interrupt_mask; M MI mask + Status', 'high'),
    0x8006ED50: ('__osDisableInt', 'M Status IE clear; 22 callers', 'high'),
    0x8006ED70: ('__osRestoreInt', 'M Status restore; 22 callers', 'high'),
    # --- thread/exception internals (replaced by the runtime, never recompiled)
    0x8006EDD0: ('__osException', 'M all RCP MMIO + Cause/EPC; no callers', 'high'),
    0x8006F304: ('send_mesg', 'S __osException helper: PopThread/EnqueueThread', 'high'),
    0x8006F3EC: ('__osEnqueueAndYield', 'M MI mask + Status; 6 callers', 'high'),
    0x8006F4EC: ('__osEnqueueThread', 'S priority-ordered insert, 6 callers', 'high'),
    0x8006F534: ('__osPopThread', 'S 0x10 leaf, 7 callers', 'high'),
    0x8006F544: ('__osDispatchThread', 'M restores context, eret', 'high'),
    0x8006ED90: ('__osDequeueThread', 'S used by osSetThreadPri/osDestroyThread', 'high'),
    # --- managers
    0x8006B4C0: ('osCreateViManager', 'S osCreateMesgQueue,osSetEventMesg,osGetThreadPri,osCreateThread', 'high'),
    0x8006B648: ('viMgrMain', 'S thread entry: __osViGetCurrentContext,osRecvMesg,__osViSwapContext,__osTimerInterrupt', 'high'),
    0x8006B820: ('osCreatePiManager', 'S osCreateMesgQueue,__osPiCreateAccessQueue,osSetEventMesg,osCreateThread', 'high'),
    0x80074560: ('__osDevMgrMain', 'S thread entry: osRecvMesg,EPi raw DMA,osSendMesg', 'high'),
    0x80068E10: ('osPiStartDma', 'H graphics_pi_dma; S osPiGetCmdQueue + Jam/Send', 'high'),
    0x8006EBD0: ('osPiGetCmdQueue', 'S 0x30 leaf called by osPiStartDma', 'high'),
    0x800742E0: ('__osPiCreateAccessQueue', 'S osCreateMesgQueue,osSendMesg', 'high'),
    0x80074330: ('__osPiGetAccess', 'S __osPiCreateAccessQueue,osRecvMesg', 'high'),
    0x80074374: ('__osPiRelAccess', 'S osSendMesg', 'high'),
    0x80074480: ('__osPiRawStartDma', 'M PI registers', 'high'),
    0x80072DA0: ('__osPiRawReadIo', 'H model_pi_read; M PI', 'high'),
    # --- VI
    0x8006B0F0: ('osViSetMode', 'H event_mode/boundary_vi_mode', 'high'),
    0x8006B160: ('osViSetSpecialFeatures', 'H event_features', 'high'),
    0x8006B320: ('osViBlack', 'H event_black', 'high'),
    0x8006B390: ('osViSwapBuffer', 'H event_vi_swap', 'high'),
    0x8006F6D0: ('__osViInit', 'M VI; S bzero + __osViSwapContext', 'high'),
    0x80073F60: ('__osViGetCurrentContext', 'S 0x10 leaf used by viMgrMain', 'high'),
    0x80073F70: ('__osViSwapContext', 'M VI registers', 'high'),
    # --- timers
    0x80073B40: ('__osTimerServicesInit', 'S called by osCreateViManager', 'high'),
    0x80073BCC: ('__osTimerInterrupt', 'S called by viMgrMain', 'high'),
    0x80073D44: ('__osSetTimerIntr', 'S osGetCount + __osSetCompare', 'high'),
    0x80073DB8: ('__osInsertTimer', 'S Disable/Restore list insert', 'high'),
    0x80077C90: ('__osSetCompare', 'M mtc0 Compare', 'high'),
    # --- SP / RDP
    0x8006AF0C: ('osSpTaskLoad', 'H graphics_sp_load', 'high'),
    0x8006B06C: ('osSpTaskStartGo', 'H graphics_sp_start', 'high'),
    0x8006AD20: ('osSpTaskYield', 'H event_yield', 'high'),
    0x8006AD40: ('osSpTaskYielded', 'H event_yielded', 'high'),
    0x80073710: ('__osSpSetStatus', 'M SP status write', 'high'),
    0x80073720: ('__osSpGetStatus', 'M SP status read', 'high'),
    0x80073A40: ('__osSpSetPc', 'M SP PC', 'high'),
    0x80073A80: ('__osSpRawStartDma', 'M SP DMA', 'high'),
    0x80073B10: ('__osSpDeviceBusy', 'M SP status', 'high'),
    # --- AI
    0x80069090: ('osAiSetFrequency', 'H game_ai_frequency; M AI', 'high'),
    0x80069390: ('osAiSetNextBuffer', 'H runtime_ai_submit; M AI', 'high'),
    0x80069440: ('osAiGetLength', 'H game_ai_length; M AI', 'high'),
    0x8006F8F0: ('__osAiDeviceBusy', 'M AI status, called by osAiSetNextBuffer', 'high'),
    # --- caches, TLB, CPU state
    0x8006ADC0: ('osWritebackDCacheAll', 'H graphics_dcache_all', 'high'),
    0x8006B9B0: ('osWritebackDCache', 'H main_cache_writeback', 'high'),
    0x80068D60: ('osInvalDCache', 'H startup_cache_invalidate', 'high'),
    0x80072CC0: ('osInvalICache', 'H model_icache; M cache ops', 'high'),
    0x80069310: ('osVirtualToPhysical', 'H game_audio_phys; S __osProbeTLB', 'high'),
    0x8006F830: ('__osProbeTLB', 'H audio_tlb; M tlbp/EntryHi', 'high'),
    0x80072D40: ('osUnmapTLBAll', 'H model_map_tlb; M tlbwi loop', 'high'),
    0x80072BF0: ('__osSetSR', 'H hangtime_set_status; M mtc0 Status', 'high'),
    0x80072C00: ('__osGetSR', 'H hangtime_get_status; M mfc0 Status', 'high'),
    0x80072C10: ('__osSetFpcCsr', 'H model_set_fpcsr', 'high'),
    0x8006A620: ('osInitialize', 'H hangtime_os_initialize', 'high'),
    # --- SI / controllers
    0x80072C20: ('__osSiRawReadIo', 'H model_si_read; M SI', 'high'),
    0x80072C70: ('__osSiRawWriteIo', 'H model_si_write; M SI', 'high'),
    0x800749D0: ('__osSiRawStartDma', 'M SI DMA + cache maintenance', 'high'),
    0x80077B70: ('__osSiDeviceBusy', 'M SI status', 'high'),
    0x80074910: ('__osSiCreateAccessQueue', 'S osCreateMesgQueue,osSendMesg', 'high'),
    0x80074960: ('__osSiGetAccess', 'S __osSiCreateAccessQueue,osRecvMesg', 'high'),
    0x800749A4: ('__osSiRelAccess', 'S osSendMesg', 'high'),
    0x8006BD20: ('osContInit', 'H main_cont_init', 'high'),
    0x8006BAC0: ('osContStartReadData', 'H main_cont_start', 'high'),
    0x8006BB84: ('osContGetReadData', 'H main_cont_read', 'high'),
    0x8006BC2C: ('__osPackReadData', 'S called by osContStartReadData', 'high'),
    0x8006BF18: ('__osContGetInitData', 'S osContInit helper', 'medium'),
    0x8006BFE8: ('__osPackRequestData', 'S osContInit helper', 'medium'),
    0x8006C0E0: ('osPfsIsPlug', 'H main_pak_query', 'high'),
    0x8006C450: ('osPfsInitPak', 'S __osSiGetAccess,__osPfsGetStatus,__osSiRelAccess,__osGetId; called after osPfsIsPlug by all 8 game pak paths', 'medium'),
    0x8006C280: ('__osPfsRequestData', 'S osPfsIsPlug helper', 'medium'),
    0x8006C37C: ('__osPfsGetInitData', 'S osPfsIsPlug helper', 'medium'),
}

if __name__ == '__main__':
    # python libultra_map.py --cpp full_runtime/libultra_names.inc : the address -> name table for the
    # runtime's own partition (full_runtime/partition.cpp), generated from this file.
    import sys
    if '--cpp' in sys.argv:
        out = sys.argv[sys.argv.index('--cpp') + 1]
        with open(out, 'w', newline='\n') as f:
            f.write('// Generated by libultra_map.py --cpp (do not edit): libultra routine names by address.\n')
            for a in sorted(LIBULTRA): f.write(f'{{0x{a:08X}u, "{LIBULTRA[a][0]}"}},\n')
        print(f'{out}: {len(LIBULTRA)} names')
