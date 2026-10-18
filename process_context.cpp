// Adapted and corrected from Claude research; see PROCESS_RESTORE.md.
// process_context.cpp — host stackful contexts for original game processes.
// Emulates 0x80068C40 save / 0x80068C8C restore / 0x80068D0C entry semantics.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <unordered_map>
#include <vector>
#include <iterator>
#include <algorithm>
#include <array>
#include "thread_memory.h"   // recomp.h + MEM_W via thread_word (word-swapped RDRAM)

extern "C" recomp_func_t* get_function(int32_t);          // thread_harness.cpp
static void fail(const char* m, uint32_t a = 0) { fprintf(stderr, "FAIL: process context: %s %08X\n", m, a); std::abort(); }

// ----------------------------------------------------------- platform shim
struct HostFiber;
using FiberProc = void (*)(void*);
static HostFiber* fiber_root_acquire();
static void       fiber_root_release(HostFiber*);
static HostFiber* fiber_create(size_t bytes, FiberProc, void* arg);
static void       fiber_switch(HostFiber* from, HostFiber* to);
static void       fiber_destroy(HostFiber*);

#if defined(_WIN32)
#include <windows.h>
struct HostFiber { LPVOID h; bool converted; FiberProc proc; void* arg; };
static VOID CALLBACK win_tramp(LPVOID p) { auto* f = (HostFiber*)p; f->proc(f->arg); fail("fiber proc returned"); }
static HostFiber* fiber_root_acquire() {
    auto* f = new HostFiber{nullptr, false, nullptr, nullptr};
    if (IsThreadAFiber()) f->h = GetCurrentFiber();
    else { f->h = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH); f->converted = true; }
    if (!f->h) fail("ConvertThreadToFiberEx");
    return f;
}
static void fiber_root_release(HostFiber* f) { if (f->converted && !ConvertFiberToThread()) fail("ConvertFiberToThread"); delete f; }
static HostFiber* fiber_create(size_t bytes, FiberProc proc, void* arg) {
    auto* f = new HostFiber{nullptr, false, proc, arg};
    f->h = CreateFiberEx(64 * 1024, bytes, FIBER_FLAG_FLOAT_SWITCH, win_tramp, f);
    if (!f->h) fail("CreateFiberEx");
    return f;
}
static void fiber_switch(HostFiber*, HostFiber* to) { SwitchToFiber(to->h); }
static void fiber_destroy(HostFiber* f) { DeleteFiber(f->h); delete f; }
#else
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
struct HostFiber { ucontext_t uc; void* map = nullptr; size_t map_size = 0; FiberProc proc = nullptr; void* arg = nullptr; };
static thread_local HostFiber* t_starting;
static void posix_tramp() { HostFiber* f = t_starting; f->proc(f->arg); fail("fiber proc returned"); }
static HostFiber* fiber_root_acquire() { return new HostFiber{}; }
static void fiber_root_release(HostFiber* f) { delete f; }
static HostFiber* fiber_create(size_t bytes, FiberProc proc, void* arg) {
    auto* f = new HostFiber{}; f->proc = proc; f->arg = arg;
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    f->map_size = bytes + page;
    f->map = mmap(nullptr, f->map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (f->map == MAP_FAILED) fail("mmap fiber stack");
    if (mprotect(f->map, page, PROT_NONE) != 0) fail("guard page");
    if (getcontext(&f->uc) != 0) fail("getcontext");
    f->uc.uc_stack.ss_sp = (char*)f->map + page; f->uc.uc_stack.ss_size = bytes; f->uc.uc_link = nullptr;
    makecontext(&f->uc, posix_tramp, 0);
    return f;
}
static void fiber_switch(HostFiber* from, HostFiber* to) { t_starting = to; if (swapcontext(&from->uc, &to->uc) != 0) fail("swapcontext"); }
static void fiber_destroy(HostFiber* f) { munmap(f->map, f->map_size); delete f; }
#endif

// ----------------------------------------------------------- controller
enum class Pending : uint8_t { None, Resume, Restart, Kill };
enum class PState  : uint8_t { Created, Running, Suspended, Finished };
struct Controller;
struct Process {
    Controller* ctl; HostFiber* fiber = nullptr;
    uint32_t stack_top = 0, entry_pc = 0, entry_a0 = 0;
    PState state = PState::Created; Process* return_to = nullptr;
    uint32_t checked_sp = 0;
    std::vector<uint8_t> suspended_stack; // opt-in original-game validation only
};
struct SaveRecord { Process* owner; uintptr_t frame; uint32_t resume_pc; std::array<int32_t,12> saved_words{}; };
struct Controller {
    uint8_t* rdram = nullptr; recomp_context* ctx = nullptr;
    Process root{}; Process* current = nullptr;
    std::unordered_map<uint32_t, std::unique_ptr<Process>> procs;   // by stack top
    std::unordered_map<uint32_t, SaveRecord> saves;                  // by guest buffer
    Pending pending = Pending::None; uint32_t p_pc = 0, p_buf = 0; uintptr_t p_frame = 0;
    std::exception_ptr forwarded;
    std::vector<std::unique_ptr<Process>> graveyard;
    size_t stack_bytes = 1u << 20;   // host stack per process (reserve); see §6
};
static thread_local Controller* t_ctl;

static Controller& controller(uint8_t* rdram, recomp_context* ctx) {
    if (!t_ctl) {
        t_ctl = new Controller{}; t_ctl->rdram = rdram; t_ctl->ctx = ctx;
        t_ctl->root.ctl = t_ctl; t_ctl->root.fiber = fiber_root_acquire();
        t_ctl->root.state = PState::Running; t_ctl->current = &t_ctl->root;
        // Fixed bounded host reserve; guest stack size remains game-owned.
    }
    if (t_ctl->rdram != rdram || t_ctl->ctx != ctx) fail("one recomp_context per OS thread (A1)");
    return *t_ctl;
}
static void drop_saves_of(Controller& c, Process* p) {
    for (auto it = c.saves.begin(); it != c.saves.end();) it = (it->second.owner == p) ? c.saves.erase(it) : std::next(it);
}
static bool referenced(Controller& c, Process* p) {
    for (auto& [buf, r] : c.saves) if (r.owner == p) return true;
    return false;
}
static void collect(Controller& c) {   // never destroys the running context
    for (auto& p : c.graveyard) fiber_destroy(p->fiber);
    c.graveyard.clear();
}
static void transfer(Controller& c, Process* to) {
    Process* from = c.current;
    if (to == from || to->state == PState::Finished || to->state == PState::Running) fail("bad transfer target", to->stack_top);
    from->state = PState::Suspended; to->state = PState::Running; c.current = to;
    fiber_switch(from->fiber, to->fiber);
    // ---- resumed: someone transferred back to this context ----
    collect(c);
    if (c.forwarded && c.current == &c.root) { auto e = c.forwarded; c.forwarded = nullptr; std::rethrow_exception(e); }
}
[[noreturn]] static void finish(Controller& c, Process* self) {
    self->state = PState::Finished; drop_saves_of(c, self);
    Process* to = (c.forwarded || !self->return_to) ? &c.root : self->return_to;
    // Move ownership to the graveyard; the fiber is destroyed later from another context.
    auto it = c.procs.find(self->stack_top);
    if (it != c.procs.end() && it->second.get() == self) { c.graveyard.push_back(std::move(it->second)); c.procs.erase(it); }
    to->state = PState::Running; c.current = to;
    fiber_switch(self->fiber, to->fiber);
    fail("finished context resumed", self->stack_top);
    std::abort();
}
static void process_main(void* arg) {
    auto* self = static_cast<Process*>(arg); Controller& c = *self->ctl;
    try {
        for (;;) {
            // Original 0x80068D0C: sp = a0 (stack top); jr a1; a0 = a2.
            c.ctx->r29 = (int64_t)(int32_t)self->stack_top;
            c.ctx->r4  = (int64_t)(int32_t)self->entry_a0;
            recomp_func_t* f = get_function((int32_t)self->entry_pc);
            if (!f) fail("process entry target", self->entry_pc);
            f(c.rdram, c.ctx);
            const Pending p = c.pending; c.pending = Pending::None;
            if (p == Pending::Restart) continue;                 // 0x80063D00 path
            if (p == Pending::Kill) break;
            if (p == Pending::Resume) fail("resume frame not on this process stack", c.p_pc);
            fail("process body returned without a context transfer", self->entry_pc);
        }
    } catch (...) { c.forwarded = std::current_exception(); }
    finish(c, self);
}
static void kill(Controller& c, Process* p) {
    if (p == c.current || p->state != PState::Suspended) fail("kill target not suspended", p->stack_top);
    drop_saves_of(c, p);
    p->return_to = c.current; c.pending = Pending::Kill;
    transfer(c, p);                                // p unwinds to process_main, then finish() returns here
    if (c.pending != Pending::None) fail("kill left pending state");
}
static void reap(Controller& c) {                  // A4: unreachable suspended contexts
    // A saved context exists only in its guest buffer. Once the game frees the owning object and
    // reuses that memory, the original restore could never resume it: retire such records.
    uint8_t* rdram = c.rdram;
    for (auto it = c.saves.begin(); it != c.saves.end();) {
        bool intact = true;
        for (size_t i = 0; i < it->second.saved_words.size() && intact; ++i)
            intact = MEM_W(i*4, it->first) == it->second.saved_words[i];
        it = (intact || it->second.owner == c.current) ? std::next(it) : c.saves.erase(it);
    }
    std::vector<Process*> dead;
    for (auto& [top, p] : c.procs) if (p->state == PState::Suspended && !referenced(c, p.get())) dead.push_back(p.get());
    for (Process* p : dead) kill(c, p);
}

// ----------------------------------------------------------- generated-code API
extern "C" int hangtime_unwinding(void) { return t_ctl && t_ctl->pending != Pending::None; }

extern "C" int hangtime_process_always_enabled;   // whole-program build: no per-resume trace
extern "C" uint32_t hangtime_claim_resume(uintptr_t frame) {
    Controller* c = t_ctl;
    if (!c || c->pending != Pending::Resume || c->p_frame != frame) return 0;
    auto it = c->saves.find(c->p_buf);
    if (it == c->saves.end() || it->second.owner != c->current || it->second.frame != frame)
        fail("resume ownership changed", c->p_buf);
    if (getenv("HANGTIME_PROCESS_RUN")) {
        // Compare at the actual generated resume label, after host-frame unwinding,
        // against the original save routine's earlier memory output.
        const auto* ctx = c->ctx;
        const std::array<gpr,12> gprs{ctx->r31,ctx->r29,ctx->r30,ctx->r16,ctx->r17,ctx->r18,
            ctx->r19,ctx->r20,ctx->r21,ctx->r22,ctx->r23,ctx->r28};
        std::array<int64_t,12> regs{};   // same two's-complement view as before (no narrowing init)
        for (size_t i=0;i<regs.size();++i) regs[i]=(int64_t)gprs[i];
        for (size_t i=0;i<regs.size();++i)
            if (regs[i] != (int64_t)it->second.saved_words[i]) fail("resume register differs from original save", c->p_pc);
        printf("PROCESS REGISTERS PASS pc=%08X words=12\n",c->p_pc);
    }
    c->pending = Pending::None;
    if(c->p_pc>=0x80000000u && !hangtime_process_always_enabled)printf("PROCESS RESUME pc=%08X sp=%08X v0=%08X s0=%08X s1=%08X root=%d\n",c->p_pc,(uint32_t)c->ctx->r29,(uint32_t)c->ctx->r2,(uint32_t)c->ctx->r16,(uint32_t)c->ctx->r17,c->current==&c->root);
    return c->p_pc;
}
extern "C" void hangtime_resume_miss(recomp_context*, uint32_t) { fail("claimed resume pc has no label in this function"); }

extern "C" void hangtime_forget_frame(uintptr_t frame) {
    if (!t_ctl) return;
    for (auto i=t_ctl->saves.begin();i!=t_ctl->saves.end();)
        i=(i->second.owner==t_ctl->current && i->second.frame==frame)?t_ctl->saves.erase(i):std::next(i);
}
extern "C" size_t hangtime_process_count() { return t_ctl ? t_ctl->procs.size() : 0; }
extern "C" int hangtime_process_is_root() { return !t_ctl || t_ctl->current==&t_ctl->root; }
// Restoration is opt-in for the harness (HANGTIME_PROCESS_RESTORE); the whole-program build
// always uses it and sets this flag before the game starts.
extern "C" int hangtime_process_always_enabled = 0;
extern "C" void hangtime_note_save(uint8_t* rdram, recomp_context* ctx, uint32_t resume_pc, uintptr_t frame) {
    if (!hangtime_process_always_enabled && !getenv("HANGTIME_PROCESS_RESTORE")) return;
    Controller& c = controller(rdram, ctx);
    const uint32_t buf = (uint32_t)ctx->r4;                       // save leaves a0 intact [V]
    if (ctx->r2 != 0) fail("save must return 0 on the direct path", resume_pc);
    if ((uint32_t)MEM_W(0, buf) != resume_pc) fail("save ra word is not the materialized link", buf);
    if (c.pending != Pending::None) fail("save during pending transfer");
    SaveRecord record{c.current, frame, resume_pc};
    for (size_t i=0;i<record.saved_words.size();++i) record.saved_words[i]=MEM_W(i*4,buf);
    c.saves[buf] = record;
}

// Original 0x80068C8C. Called as a JAL target (hook), so ctx->r4 = buffer, r5 = value.
extern "C" void hangtime_context_restore(uint8_t* rdram, recomp_context* ctx) {
    Controller& c = controller(rdram, ctx);
    if (c.pending != Pending::None) fail("nested transfer");
    reap(c);
    const uint32_t buf = (uint32_t)ctx->r4;
    auto it = c.saves.find(buf);
    if (it == c.saves.end()) fail("restore of unrecorded or expired context", buf);
    const SaveRecord r = it->second;
    if (r.owner->state == PState::Finished) fail("restore into finished context", buf);
    // Test-mode oracle: sibling processes must not alter a suspended active stack.
    // Capture before loading the destination registers, not after the shared SP changes.
    if (getenv("HANGTIME_PROCESS_RUN") && r.owner != c.current) {
        Process* from = c.current;
        if (from != &c.root) {
            const uint32_t sp = (uint32_t)ctx->r29, top = from->stack_top;
            if (sp < 0x80000000u || top > 0x80800000u || sp > top) fail("guest stack bounds", sp);
            from->checked_sp = sp;
            from->suspended_stack.assign(rdram + (sp & 0x7fffff), rdram + (top - 0x80000000u));
        }
        if (r.owner != &c.root && !r.owner->suspended_stack.empty()) {
            const auto& bytes = r.owner->suspended_stack;
            if (!std::equal(bytes.begin(), bytes.end(), rdram + (r.owner->checked_sp & 0x7fffff)))
                fail("suspended guest stack changed", r.owner->checked_sp);
            printf("PROCESS STACK PASS sp=%08X top=%08X bytes=%zu\n",r.owner->checked_sp,r.owner->stack_top,bytes.size());
        }
    }
    // ---- original semantics, in order ----
    ctx->r2  = ctx->r5;                           // move v0,a1
    ctx->r31 = MEM_W(0x00, buf); ctx->r29 = MEM_W(0x04, buf); ctx->r30 = MEM_W(0x08, buf);
    ctx->r16 = MEM_W(0x0C, buf); ctx->r17 = MEM_W(0x10, buf); ctx->r18 = MEM_W(0x14, buf);
    ctx->r19 = MEM_W(0x18, buf); ctx->r20 = MEM_W(0x1C, buf); ctx->r21 = MEM_W(0x20, buf);
    ctx->r22 = MEM_W(0x24, buf); ctx->r23 = MEM_W(0x28, buf); ctx->r28 = MEM_W(0x2C, buf);
    if (ctx->r2 == 0) ctx->r2 = 1;                // bnez v0 / addiu v0,zero,1
    if ((uint32_t)ctx->r31 != r.resume_pc) fail("saved ra does not match recorded resume site", buf);
    c.pending = Pending::Resume; c.p_pc = r.resume_pc; c.p_frame = r.frame; c.p_buf = buf;
    if (r.owner != c.current) transfer(c, r.owner);
    // return: post-call hooks unwind to the saving frame (same or switched context)
}

// Original 0x80068D0C (replaces the nesting adapter).
extern "C" void hangtime_process_enter(uint8_t* rdram, recomp_context* ctx) {
    Controller& c = controller(rdram, ctx);
    if (c.pending != Pending::None) fail("nested transfer");
    const uint32_t top = (uint32_t)ctx->r4, pc = (uint32_t)ctx->r5, arg = (uint32_t)ctx->r6;
    Process* cur = c.current;
    if (cur != &c.root && cur->stack_top == top) {             // discard own stack and restart
        cur->entry_pc = pc; cur->entry_a0 = arg; drop_saves_of(c, cur);
        c.pending = Pending::Restart; return;
    }
    reap(c);
    if (auto it = c.procs.find(top); it != c.procs.end()) kill(c, it->second.get());
    auto p = std::make_unique<Process>(); p->ctl = &c;
    p->stack_top = top; p->entry_pc = pc; p->entry_a0 = arg;
    p->fiber = fiber_create(c.stack_bytes, process_main, p.get());
    if(c.procs.size()>=64) {
        for (auto& [t, q] : c.procs) {
            fprintf(stderr, "  process top=%08X entry=%08X a0=%08X state=%d saves:", t, q->entry_pc, q->entry_a0, (int)q->state);
            for (auto& [buf, r] : c.saves) if (r.owner == q.get()) fprintf(stderr, " %08X->%08X", buf, r.resume_pc);
            fprintf(stderr, "\n");
        }
        fail("host process limit", top);
    }
    Process* raw = p.get(); c.procs[top] = std::move(p);
    transfer(c, raw);
}

// Teardown between fixtures / at OS-thread exit. Call only from the root context.
void hangtime_process_reset() {
    Controller* c = t_ctl; if (!c) return;
    if (c->current != &c->root) fail("reset outside root context");
    c->pending = Pending::None; c->saves.clear();
    std::vector<Process*> all; for (auto& [t, p] : c->procs) all.push_back(p.get());
    for (Process* p : all) if (p->state == PState::Suspended) kill(*c, p);
    collect(*c);
    for (auto& [t, p] : c->procs) if (p->state == PState::Created) fiber_destroy(p->fiber);
    c->procs.clear();
    fiber_root_release(c->root.fiber); delete c; t_ctl = nullptr;
}
bool hangtime_process_idle() { return !t_ctl || (t_ctl->pending == Pending::None && t_ctl->current == &t_ctl->root); }
