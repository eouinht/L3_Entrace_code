#ifndef TIMER_H
#define TIMER_H

#include <time.h>

typedef void (*timer_cb)(void *arg);

int timer_init(long period_ns, timer_cb cb, void *arg);
int timer_start(void);
void timer_stop(void);
void sleep_ns(long ns);

#endif