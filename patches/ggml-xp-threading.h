// ggml-xp-threading.h - Windows XP replacement for ggml-cpu's threadpool mutex / condition variable.
//
// ggml-cpu.c uses SRWLOCK + CONDITION_VARIABLE (Windows Vista+). This header provides the same
// ggml_mutex_* / ggml_cond_* macros using only XP-era APIs (CRITICAL_SECTION + a manual-reset event).
// Installed into ggml/src/ggml-cpu/ by scripts/patch-for-xp.sh.
//
// The condition variable is the "generation count" algorithm from Schmidt & Pyarali,
// "Strategies for Implementing POSIX Condition Variables on Win32" (section 3.2):
// waits release the external lock while blocked, broadcasts are never lost, and a waiter that
// arrives after a broadcast is not woken by it. ggml only needs wait + broadcast.

#pragma once

#include <windows.h>

typedef CRITICAL_SECTION ggml_mutex_t;

typedef struct {
    int              waiters;     // threads currently waiting
    int              release;     // threads that still have to leave after the last broadcast
    int              generation;  // incremented by every broadcast
    CRITICAL_SECTION lock;        // protects the three counters
    HANDLE           event;       // manual-reset: set by broadcast, reset by the last released waiter
} ggml_cond_t;

static inline void ggml_xp_cond_init(ggml_cond_t * c) {
    c->waiters = 0;
    c->release = 0;
    c->generation = 0;
    InitializeCriticalSection(&c->lock);
    c->event = CreateEventA(NULL, TRUE, FALSE, NULL);
}

static inline void ggml_xp_cond_destroy(ggml_cond_t * c) {
    CloseHandle(c->event);
    DeleteCriticalSection(&c->lock);
}

static inline void ggml_xp_cond_wait(ggml_cond_t * c, ggml_mutex_t * m) {
    EnterCriticalSection(&c->lock);
    c->waiters++;
    const int my_generation = c->generation;
    LeaveCriticalSection(&c->lock);

    LeaveCriticalSection(m);
    for (;;) {
        WaitForSingleObject(c->event, INFINITE);
        EnterCriticalSection(&c->lock);
        const int done = c->release > 0 && c->generation != my_generation;
        LeaveCriticalSection(&c->lock);
        if (done) break;
        Sleep(0);  // event still set for an older generation: let those waiters leave
    }
    EnterCriticalSection(m);

    EnterCriticalSection(&c->lock);
    c->waiters--;
    const int last = --c->release == 0;
    LeaveCriticalSection(&c->lock);
    if (last) ResetEvent(c->event);
}

static inline void ggml_xp_cond_broadcast(ggml_cond_t * c) {
    EnterCriticalSection(&c->lock);
    if (c->waiters > 0) {
        c->release = c->waiters;
        c->generation++;
        SetEvent(c->event);
    }
    LeaveCriticalSection(&c->lock);
}

#define ggml_mutex_init(m)          InitializeCriticalSection(m)
#define ggml_mutex_destroy(m)       DeleteCriticalSection(m)
#define ggml_mutex_lock(m)          EnterCriticalSection(m)
#define ggml_mutex_unlock(m)        LeaveCriticalSection(m)
#define ggml_mutex_lock_shared(m)   EnterCriticalSection(m)
#define ggml_mutex_unlock_shared(m) LeaveCriticalSection(m)

#define ggml_cond_init(c)      ggml_xp_cond_init(c)
#define ggml_cond_destroy(c)   ggml_xp_cond_destroy(c)
#define ggml_cond_wait(c, m)   ggml_xp_cond_wait(c, m)
#define ggml_cond_broadcast(c) ggml_xp_cond_broadcast(c)
