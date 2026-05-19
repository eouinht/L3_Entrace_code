#include "timer.h"

#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>

static pthread_t timer_thread;

static long preiod_ns;
static timer_cb callback;
static void *cb_arg;

static atomic_int running = 0;

static inline void timespec_add_ns(struct timespec *t, long ns){
    t->tv_nsec += ns;
    while(t->tv_nsec >= 1000000000L){
        t->tv_nsec -= 1000000000L;
        t->tv_sec++;
    }

}

static void *timer_loop(void *arg){
    struct timespec next;

    clock_gettime(CLOCK_MONOTONIC, &next);
    while(atomic_load(&running)){
        timespec_add_ns(&next, preiod_ns);
        clock_nanosleep(
            CLOCK_MONOTONIC,
            TIMER_ABSTIME,
            &next,
            NULL
        );
        if (atomic_load(&running) && callback){
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
    atomic_store(&running, 1);
    return pthread_create(
        &timer_thread, 
        NULL, 
        timer_loop, 
        NULL);
}

void timer_stop(void){
    atomic_store(&running, 0);
    pthread_join(timer_thread, NULL);
}

void sleep_ns(long ns)
{
    struct timespec ts;

    ts.tv_sec = ns / 1000000000L;
    ts.tv_nsec = ns % 1000000000L;

    nanosleep(&ts, NULL);
}