#include "timer.h"
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

static pthread_t timer_thread;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

static long preiod_ns;
static timer_cb callback;
static void *cb_arg;
static int running = 0;

static inline void timespec_add_ns(struct timespec *t, long ns){
    t->tv_sec += ns;
    while(t->tv_sec >= 1000000000L){
        t->tv_sec -= 1000000000L;
        t->tv_sec++;
    }

}
static void *timer_loop(void *arg){
    struct timespec next;

    clock_gettime(CLOCK_MONOTONIC, &next);
    while(running){
        timespec_add_ns(&next, preiod_ns);

        pthread_mutex_lock(&mtx);
        pthread_cond_timedwait(&cv, &mtx, &next);
        pthread_mutex_unlock(&mtx);
        if (running && callback){
            callback(cb_arg);
        }
    }
    return NULL;
}

int timer_init(long p_ns, timer_cb cb, void* arg){
    preiod_ns = p_ns;
    callback = cb;
    cb_arg = arg;
    return 0;
}

int timer_start(void){
    running = 1;
    return pthread_create(&timer_thread, NULL, timer_loop, NULL);
}

void timer_stop(void){
    running = 0;
    pthread_cond_signal(&cv);
    pthread_join(timer_thread, NULL);
}
