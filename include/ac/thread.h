#pragma once

#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE ac_thread_handle_t;
#else
#include <pthread.h>
typedef pthread_t ac_thread_handle_t;
#endif

typedef void *(*ac_thread_fn_t)(void *arg);

typedef struct {
    ac_thread_handle_t handle;
    int started;
} ac_thread_t;

int ac_thread_start(ac_thread_t *thread, ac_thread_fn_t fn, void *arg);
int ac_thread_join(ac_thread_t *thread);
void ac_thread_sleep_ms(uint32_t ms);
