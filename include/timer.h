#ifndef TIMER_H
#define TIMER_H

#include <time.h>

/**
 * Type of callback function called periodically by the timer.
 * @param arg User-provided argument passed from timer_init().
 */
typedef void (*timer_cb)(void *arg);

/**
 * Initialize the timer configuration.
 *
 * @param p_ns Timer period in nanoseconds.
 * @param cb Callback function to call on each timer tick.
 * @param arg User data passed to the callback.
 *
 * @return 0 on success.
 */
int timer_init(long period_ns, timer_cb cb, void *arg);

/**
 * Start the timer thread.
 *
 * @return 0 on success, or pthread_create() error code on failure.
 */
int timer_start(void);

/** 
 *Stop the timer thread and wait until it finishes.
 */
void timer_stop(void);

/**
 * Sleep for a given number of nanoseconds.
 *
 * @param ns Sleep duration in nanoseconds.
 */
void sleep_ns(long ns);

#endif