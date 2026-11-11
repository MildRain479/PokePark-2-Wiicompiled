/* Guest threads as host fibers, and interrupts taken on them.
 *
 * The OS switches threads in SelectThread: OSSaveContext stores the
 * current thread's registers (returning 0 now and 1 when the thread is
 * resumed, like setjmp), then OSLoadContext restores the next thread's
 * and continues at its saved PC with rfi. Recompiled code keeps its call
 * chain on the host stack, so each guest thread gets its own fiber, and
 * each fiber remembers where it must continue when its context is
 * loaded again (its "resume point"):
 *  - OSSaveContext inside SelectThread: continue by returning from that
 *    SelectThread, which is what "OSSaveContext returned 1" leads to;
 *  - switched out from inside an interrupt handler without a save (the
 *    context was saved by the exception): continue the interrupted code.
 * Only one fiber runs at a time, as on the Wii's single CPU. */
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "hle.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define FIBER_API WINAPI
#define fiber_self() ConvertThreadToFiber(NULL)
#define fiber_new(size, fn, arg) CreateFiber(size, fn, arg)
#define fiber_free(h) DeleteFiber(h)
#define fiber_switch(from, to) SwitchToFiber(to)
#else
/* Elsewhere, fibers of our own: a stack each, and a switch that pushes
 * the callee-saved registers on the stack being left, then pops them
 * from the one being entered. A new fiber's stack starts as if it had
 * been switched out just before host_fiber_start, which calls fn(arg). */
#include <sys/mman.h>
#define FIBER_API

typedef struct {
    void* sp; /* saved while switched out */
    void* stack;
    size_t size;
} HostFiber;

void host_fiber_swap(void** save_sp, void* sp);
void host_fiber_start(void);

#if defined(__aarch64__)
__asm__(".text\n"
        ".globl host_fiber_swap\n"
        ".type host_fiber_swap, %function\n"
        ".p2align 2\n"
        "host_fiber_swap:\n"
        "  sub sp, sp, #160\n"
        "  stp x19, x20, [sp, #0]\n"
        "  stp x21, x22, [sp, #16]\n"
        "  stp x23, x24, [sp, #32]\n"
        "  stp x25, x26, [sp, #48]\n"
        "  stp x27, x28, [sp, #64]\n"
        "  stp x29, x30, [sp, #80]\n"
        "  stp d8, d9, [sp, #96]\n"
        "  stp d10, d11, [sp, #112]\n"
        "  stp d12, d13, [sp, #128]\n"
        "  stp d14, d15, [sp, #144]\n"
        "  mov x9, sp\n"
        "  str x9, [x0]\n"
        "  mov sp, x1\n"
        "  ldp x19, x20, [sp, #0]\n"
        "  ldp x21, x22, [sp, #16]\n"
        "  ldp x23, x24, [sp, #32]\n"
        "  ldp x25, x26, [sp, #48]\n"
        "  ldp x27, x28, [sp, #64]\n"
        "  ldp x29, x30, [sp, #80]\n"
        "  ldp d8, d9, [sp, #96]\n"
        "  ldp d10, d11, [sp, #112]\n"
        "  ldp d12, d13, [sp, #128]\n"
        "  ldp d14, d15, [sp, #144]\n"
        "  add sp, sp, #160\n"
        "  ret\n"
        ".globl host_fiber_start\n"
        ".type host_fiber_start, %function\n"
        "host_fiber_start:\n"
        "  mov x0, x20\n"
        "  blr x19\n"
        "  brk #0\n");
#define SWAP_FRAME 160
static void fiber_frame(uintptr_t* f, void (*fn)(void*), void* arg) {
    f[0] = (uintptr_t)fn; /* x19 */
    f[1] = (uintptr_t)arg; /* x20 */
    f[11] = (uintptr_t)host_fiber_start; /* x30 */
}
#elif defined(__x86_64__)
__asm__(".text\n"
        ".globl host_fiber_swap\n"
        "host_fiber_swap:\n"
        "  pushq %rbp\n"
        "  pushq %rbx\n"
        "  pushq %r12\n"
        "  pushq %r13\n"
        "  pushq %r14\n"
        "  pushq %r15\n"
        "  movq %rsp, (%rdi)\n"
        "  movq %rsi, %rsp\n"
        "  popq %r15\n"
        "  popq %r14\n"
        "  popq %r13\n"
        "  popq %r12\n"
        "  popq %rbx\n"
        "  popq %rbp\n"
        "  ret\n"
        ".globl host_fiber_start\n"
        "host_fiber_start:\n"
        "  movq %r13, %rdi\n"
        "  call *%r12\n"
        "  ud2\n");
/* Six registers and the return address, placed so the stack is 16-byte
 * aligned at host_fiber_start's call. */
#define SWAP_FRAME 72
static void fiber_frame(uintptr_t* f, void (*fn)(void*), void* arg) {
    f[2] = (uintptr_t)arg; /* r13 */
    f[3] = (uintptr_t)fn; /* r12 */
    f[6] = (uintptr_t)host_fiber_start;
}
#else
#error "no fiber switch for this CPU"
#endif

static void* fiber_self(void) { return calloc(1, sizeof(HostFiber)); }

static void* fiber_new(size_t size, void (*fn)(void*), void* arg) {
    HostFiber* f = (HostFiber*)calloc(1, sizeof(HostFiber));
    uint8_t* top;
    f->stack = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (f->stack == MAP_FAILED) {
        HLE_WARN("no memory for a thread's stack");
        exit(1);
    }
    f->size = size;
    mprotect(f->stack, 4096, PROT_NONE); /* a guard page under it */
    top = (uint8_t*)f->stack + size;
    f->sp = top - SWAP_FRAME;
    memset(f->sp, 0, SWAP_FRAME);
    fiber_frame((uintptr_t*)f->sp, fn, arg);
    return f;
}

static void fiber_free(void* h) {
    HostFiber* f = (HostFiber*)h;
    if (f->stack) munmap(f->stack, f->size);
    free(f);
}

static void fiber_switch(void* from, void* to) { host_fiber_swap(&((HostFiber*)from)->sp, ((HostFiber*)to)->sp); }
#endif

/* MSVC's longjmp unwinds through SEH, which rejects jumps on fiber
 * stacks (STATUS_BAD_STACK). With the buffer's Frame cleared it only
 * restores registers, which is all a jump within one fiber needs. */
#if defined(_WIN32) && defined(_M_X64)
#define SETJMP(jb) (setjmp(jb) ? 1 : (((_JUMP_BUFFER*)(jb))->Frame = 0, 0))
#else
#define SETJMP(jb) setjmp(jb)
#endif

#define MAX_THREADS 64
#define MAX_NESTED_INTERRUPTS 8
#define FIBER_STACK (8u << 20)

/* A place a fiber can continue from: a jump target plus the guest
 * registers to restore there. Lives on the fiber's own stack. */
typedef struct Resume {
    jmp_buf jb;
    ppc_t regs;
    struct Resume* outer; /* enclosing SelectThread frame, if any */
} Resume;

typedef struct Fiber {
    uint32_t ctx; /* guest OSContext this fiber runs */
    void* handle;
    Resume* resume;  /* where to continue when ctx is loaded */
    Resume* select;  /* innermost active SelectThread frame */
    Resume* irq[MAX_NESTED_INTERRUPTS];
    uint32_t irq_ctx[MAX_NESTED_INTERRUPTS];
    int irq_depth;
    int exited; /* its thread called OSExitThread; the slot can be reused */
} Fiber;

static Fiber sFibers[MAX_THREADS];
static int sFiberCount;
static Fiber* sCurrent;
static ppc_t* sCpu;

/* SDK addresses. */
static const uint32_t kOSExitThread = A_OSExitThread;
static const uint32_t kIdleLoopReturn = A_SelectThread + 0x14C; /* SelectThread, after OSEnableInterrupts */
static const uint32_t kRunQueueBits = A_RUN_QUEUE_BITS;
static const uint32_t kCurrentContext = 0x800000D4;

static Fiber* fiber_for(uint32_t ctx) {
    int i;
    for (i = 0; i < sFiberCount; i++) {
        if (sFibers[i].ctx == ctx) return &sFibers[i];
    }
    return NULL;
}

static void ensure_main_fiber(ppc_t* c) {
    if (sCurrent) return;
    sCpu = c;
    sCurrent = &sFibers[sFiberCount++];
    sCurrent->handle = fiber_self();
}

/* Loads registers from a guest OSContext. */
static void load_context(ppc_t* c, uint32_t ctx) {
    int i;
    for (i = 0; i < 32; i++) {
        c->r[i] = LD32(ctx + 4 * i);
        c->f[i] = ld_f64(ctx + 0x90 + 8 * i);
        c->ps1[i] = ld_f64(ctx + 0x1C8 + 8 * i);
    }
    c->cr = LD32(ctx + 0x80);
    c->lr = LD32(ctx + 0x84);
    c->ctr = LD32(ctx + 0x88);
    ppc_mtspr(c, 1, LD32(ctx + 0x8C));
    c->fpscr = LD32(ctx + 0x194);
    for (i = 0; i < 8; i++) c->gqr[i] = LD32(ctx + 0x1A4 + 4 * i);
    c->msr = LD32(ctx + 0x19C);
}

static void FIBER_API fiber_main(void* arg) {
    Fiber* f = (Fiber*)arg;
    ppc_t* c = sCpu;
    uint32_t entry, ret;
    load_context(c, f->ctx);
    entry = LD32(f->ctx + 0x198); /* SRR0: the thread function */
    HLE_LOG("thread %08X starts at %08X [%s]", f->ctx, entry, ppc_func_name(entry));
    ret = c->lr;
    ppc_icall(c, entry);
    /* Returning from the thread function goes to its LR, OSExitThread. */
    if (ret != kOSExitThread) HLE_WARN("thread %08X returned to %08X", f->ctx, ret);
    /* OSExitThread switches away for good: a thread created later at the
     * same address gets a new fiber (OSLoadContext). */
    f->exited = 1;
    ppc_icall(c, kOSExitThread);
    HLE_WARN("thread %08X: OSExitThread returned", f->ctx);
    exit(1);
}

/* Continues fiber f: starts it, or jumps to its resume point. Called on
 * the fiber being left; returns only by a jump into that fiber's own
 * resume point once something switches back to it. */
static void switch_to(Fiber* f) {
    Fiber* self = sCurrent;
    if (f != self) {
        sCurrent = f;
        fiber_switch(self->handle, f->handle);
        /* Something switched back to this fiber. */
        self = sCurrent;
    }
    if (!self->resume) {
        HLE_WARN("thread %08X resumed with no resume point", self->ctx);
        exit(1);
    }
    longjmp(self->resume->jb, 1);
}

/* static BOOL SelectThread(BOOL yield) */
void F_SelectThread(ppc_t* c) {
    Resume frame;
    Fiber* self;
    ensure_main_fiber(c);
    self = sCurrent;
    memcpy(&frame.regs, c, sizeof(*c));
    frame.outer = self->select;
    self->select = &frame;
    if (SETJMP(frame.jb)) {
        /* Resumed by OSLoadContext of this thread's context. */
        self = sCurrent;
        memcpy(c, &frame.regs, sizeof(*c));
        self->select = frame.outer;
        c->r[3] = 0;
        return;
    }
    F_SelectThread_orig(c);
    self->select = frame.outer;
}

/* u32 OSSaveContext(OSContext* context): binds the context to the
 * running fiber and makes the enclosing SelectThread its resume point,
 * then saves as usual (returns 0). */
void F_OSSaveContext(ppc_t* c) {
    uint32_t ctx = c->r[3];
    ensure_main_fiber(c);
    if (!sCurrent->ctx) sCurrent->ctx = ctx;
    if (sCurrent->ctx == ctx && sCurrent->select) {
        sCurrent->resume = sCurrent->select;
        if (hle_log_enabled() > 1) {
            HLE_LOG("thread %08X switches out", ctx);
            ppc_backtrace(c);
        }
    }
    else HLE_LOG("OSSaveContext(%08X) outside a thread switch", ctx);
    F_OSSaveContext_orig(c);
    c->r[3] = 0;
}

/* void OSLoadContext(OSContext* context) */
void F_OSLoadContext(ppc_t* c) {
    uint32_t ctx = c->r[3];
    Fiber* f;
    ensure_main_fiber(c);
    if (sCurrent->irq_depth && sCurrent->irq_ctx[sCurrent->irq_depth - 1] == ctx) {
        /* An interrupt handler returning to the code it interrupted. */
        longjmp(sCurrent->irq[sCurrent->irq_depth - 1]->jb, 1);
    }
    f = fiber_for(ctx);
    if (!f || f->exited) {
        /* A new thread: in the slot of the one that ran at this address
         * before, or of any thread that has exited, or a new slot. */
        int i;
        for (i = 0; !f && i < sFiberCount; i++) {
            if (sFibers[i].exited) f = &sFibers[i];
        }
        if (!f) {
            if (sFiberCount >= MAX_THREADS) {
                HLE_WARN("too many threads");
                exit(1);
            }
            f = &sFibers[sFiberCount++];
        }
        if (f->handle) fiber_free(f->handle);
        memset(f, 0, sizeof(*f));
        f->ctx = ctx;
        f->handle = fiber_new(FIBER_STACK, fiber_main, f);
        {
            Fiber* self = sCurrent;
            sCurrent = f;
            fiber_switch(self->handle, f->handle);
        }
        /* Something switched back to this fiber. */
        longjmp(sCurrent->resume->jb, 1);
    }
    switch_to(f);
}

/* Runs fn(r3, r4) the way the CPU takes an interrupt: on the current
 * thread, with the current context marked as saved by an exception
 * (OS_CONTEXT_STATE_EXC), so a reschedule switches threads without saving
 * the handler's own state. The handler returns either normally or by
 * OSLoadContext on the interrupted context. If it switches threads, this
 * thread later continues the interrupted code.
 *
 * A device's handler (or callback) runs as __OSDispatchInterrupt runs it:
 * with the scheduler off, then a reschedule once it has returned. Threads
 * it wakes start after it, not in the middle of it: the rest of a handler
 * cut off by a switch never runs. (nw4r's streams update in the audio
 * callback; cut off after queueing a block's load but before counting it,
 * a stream loaded the same block again and fell behind its voice, which
 * then played its buffer's old contents: the music repeated bits.) The
 * decrementer's handler does that itself (DecrementerExceptionCallback
 * sets up its own context). */
static const uint32_t kOSDisableScheduler = A_OSDisableScheduler;
static const uint32_t kOSEnableScheduler = A_OSEnableScheduler;
static const uint32_t kOSReschedule = A___OSReschedule;            /* __OSReschedule */
static const uint32_t kDecrementerHandler = A_DecrementerExceptionHandler;      /* DecrementerExceptionHandler */
static int sSchedulerOff[MAX_NESTED_INTERRUPTS]; /* by depth: still to turn back on */

void hle_interrupt(ppc_t* c, uint32_t fn, uint32_t r3, uint32_t r4) {
    Resume frame;
    Fiber* self;
    Resume* saved_resume;
    Resume* saved_select;
    uint32_t ctx = LD32(kCurrentContext);
    ensure_main_fiber(c);
    self = sCurrent;
    if (self->irq_depth >= MAX_NESTED_INTERRUPTS) {
        HLE_WARN("interrupts nested too deeply");
        return;
    }
    memcpy(&frame.regs, c, sizeof(*c));
    saved_resume = self->resume;
    saved_select = self->select;
    self->irq[self->irq_depth] = &frame;
    self->irq_ctx[self->irq_depth] = ctx;
    self->irq_depth++;
    /* A switch away inside the handler resumes the interrupted code. */
    self->resume = &frame;
    if (ctx) ST16(ctx + 0x1A2, LD16(ctx + 0x1A2) | 2);
    if (!SETJMP(frame.jb)) {
        int depth = self->irq_depth - 1, dispatch = fn != kDecrementerHandler;
        c->r[1] = (c->r[1] - 0x200) & ~7u;
        if (dispatch) {
            c->lr = 0;
            ppc_icall(c, kOSDisableScheduler);
            sSchedulerOff[depth] = 1;
        }
        c->r[3] = r3;
        c->r[4] = r4;
        c->lr = 0;
        ppc_icall(c, fn);
        if (dispatch) {
            c->lr = 0;
            ppc_icall(c, kOSEnableScheduler);
            sSchedulerOff[depth] = 0;
            ppc_icall(c, kOSReschedule);
        }
    }
    self = sCurrent;
    self->irq_depth--;
    self->resume = saved_resume;
    self->select = saved_select;
    memcpy(c, &frame.regs, sizeof(*c));
    if (sSchedulerOff[self->irq_depth]) {
        /* The handler left by OSLoadContext: the scheduler back on. */
        sSchedulerOff[self->irq_depth] = 0;
        c->r[1] = (c->r[1] - 0x200) & ~7u;
        c->lr = 0;
        ppc_icall(c, kOSEnableScheduler);
        memcpy(c, &frame.regs, sizeof(*c));
    }
    if (ctx) ST16(ctx + 0x1A2, LD16(ctx + 0x1A2) & ~2u);
}

int hle_in_interrupt(void) { return sCurrent && sCurrent->irq_depth > 0; }

/* Called when the game enables interrupts. In SelectThread's idle loop
 * (no thread is runnable) the guest spins until an interrupt readies a
 * thread, so wait for one here instead. */
void hle_idle_check(ppc_t* c) {
    if (c->lr != kIdleLoopReturn) return;
    while (LD32(kRunQueueBits) == 0) {
        if (!hle_wait_for_event(c)) {
            HLE_WARN("deadlock: no runnable thread and no pending event");
            ppc_dump_trace();
            exit(5);
        }
    }
}

/* Prints where every guest thread is waiting (for the boot watchdog). */
void hle_dump_threads(void) {
    int i;
    for (i = 0; i < sFiberCount; i++) {
        Fiber* f = &sFibers[i];
        fprintf(stderr, "thread %08X%s:\n", f->ctx, f == sCurrent ? " (running)" : "");
        if (f != sCurrent && f->resume) ppc_backtrace(&f->resume->regs);
    }
}

/* BOOL OSCreateThread(OSThread* thread, void* (*func)(void*), void* param,
 * void* stack, u32 stackSize, OSPriority priority, u16 attr): a thread
 * created where an earlier one ran (one that exited or was cancelled)
 * must start on a new fiber, not continue the old one's. */
void F_OSCreateThread(ppc_t* c) {
    Fiber* f = fiber_for(c->r[3]);
    if (f && f != sCurrent) f->exited = 1;
    F_OSCreateThread_orig(c);
}
