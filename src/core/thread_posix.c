#ifndef _WIN32
#include "ac/thread.h"

#include <sys/select.h>

int ac_thread_start(ac_thread_t *thread, ac_thread_fn_t fn, void *arg)
{
    if (thread == NULL || fn == NULL) return -1;
    if (pthread_create(&thread->handle, NULL, fn, arg) != 0) {
        thread->started = 0;
        return -1;
    }
    thread->started = 1;
    return 0;
}

int ac_thread_join(ac_thread_t *thread)
{
    if (thread == NULL || !thread->started) return 0;
    if (pthread_join(thread->handle, NULL) != 0) return -1;
    thread->started = 0;
    return 0;
}

void ac_thread_sleep_ms(uint32_t ms)
{
    struct timeval tv;
    tv.tv_sec = (long)(ms / 1000u);
    tv.tv_usec = (long)((ms % 1000u) * 1000u);
    (void)select(0, NULL, NULL, NULL, &tv);
}
#endif
