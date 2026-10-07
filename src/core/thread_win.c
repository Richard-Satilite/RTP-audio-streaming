#ifdef _WIN32
#include "ac/thread.h"

#include <windows.h>

typedef struct {
    ac_thread_fn_t fn;
    void *arg;
} ac_thread_start_ctx_t;

static DWORD WINAPI ac_thread_entry(LPVOID arg)
{
    ac_thread_start_ctx_t *ctx = (ac_thread_start_ctx_t *)arg;
    ac_thread_fn_t fn = ctx->fn;
    void *fn_arg = ctx->arg;
    HeapFree(GetProcessHeap(), 0, ctx);
    (void)fn(fn_arg);
    return 0;
}

int ac_thread_start(ac_thread_t *thread, ac_thread_fn_t fn, void *arg)
{
    ac_thread_start_ctx_t *ctx;
    if (thread == NULL || fn == NULL) return -1;
    ctx = (ac_thread_start_ctx_t *)HeapAlloc(GetProcessHeap(), 0, sizeof(*ctx));
    if (ctx == NULL) return -1;
    ctx->fn = fn;
    ctx->arg = arg;
    thread->handle = CreateThread(NULL, 0, ac_thread_entry, ctx, 0, NULL);
    if (thread->handle == NULL) {
        HeapFree(GetProcessHeap(), 0, ctx);
        thread->started = 0;
        return -1;
    }
    thread->started = 1;
    return 0;
}

int ac_thread_join(ac_thread_t *thread)
{
    if (thread == NULL || !thread->started) return 0;
    if (WaitForSingleObject(thread->handle, INFINITE) != WAIT_OBJECT_0) return -1;
    CloseHandle(thread->handle);
    thread->handle = NULL;
    thread->started = 0;
    return 0;
}

void ac_thread_sleep_ms(uint32_t ms)
{
    Sleep((DWORD)ms);
}
#endif
