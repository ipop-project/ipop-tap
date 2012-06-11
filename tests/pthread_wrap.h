/*
 * Pthread wrapper functions
 *
 * Copyright (C) 2008-2009 Florent Bondoux
 *
 * This file is part of Campagnol.
 *
 * Campagnol is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Campagnol is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Campagnol.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef PTHREAD_WRAP_H_
#define PTHREAD_WRAP_H_

/*
 * Wrapper functions around a few pthread functions
 * These functions exit in case of error
 */

//#include "config.h"
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
//#include "log.h"

static inline void mutexInit(pthread_mutex_t *mutex, pthread_mutexattr_t *attrs);
static inline void mutexDestroy(pthread_mutex_t *mutex);
static inline void mutexLock(pthread_mutex_t *mutex);
static inline void mutexUnlock(pthread_mutex_t *mutex);

static inline void mutexattrInit(pthread_mutexattr_t *attrs);
static inline void mutexattrDestroy(pthread_mutexattr_t *attrs);
static inline void mutexattrSettype(pthread_mutexattr_t *attrs, int type);

static inline void conditionInit(pthread_cond_t *cond, pthread_condattr_t *attrs);
static inline void conditionDestroy(pthread_cond_t *cond);
static inline int conditionWait(pthread_cond_t *cond, pthread_mutex_t *mutex);
static inline int conditionTimedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abs_timeout);
static inline int conditionBroadcast(pthread_cond_t *cond);
static inline int conditionSignal(pthread_cond_t *cond);

static inline pthread_t createThread(void * (*start_routine)(void *), void * arg);
static inline pthread_t createDetachedThread(void * (*start_routine)(void *), void * arg);
static inline void joinThread(pthread_t thread, void **value_ptr);

void ASSERT()
{
  // do nothing
}

void
log_error(int r, const char *msg)
{
    fprintf(stderr, msg);
}

void mutexInit(pthread_mutex_t *mutex, pthread_mutexattr_t *attrs) {
    ASSERT(mutex);
    int r = pthread_mutex_init(mutex, attrs);
    if (r != 0) {
        log_error(r, "Error pthread_mutex_init()");
        abort();
    }
}

void mutexDestroy(pthread_mutex_t *mutex) {
    ASSERT(mutex);
    int r = pthread_mutex_destroy(mutex);
    if (r != 0) {
        log_error(r, "Error pthread_mutex_destroy()");
        abort();
    }
}

void mutexLock(pthread_mutex_t *mutex) {
    ASSERT(mutex);
    int r = pthread_mutex_lock(mutex);
    if (r != 0) {
        log_error(r, "Error pthread_mutex_lock()");
        abort();
    }
}

void mutexUnlock(pthread_mutex_t *mutex) {
    ASSERT(mutex);
    int r = pthread_mutex_unlock(mutex);
    if (r != 0) {
        log_error(r, "Error pthread_mutex_unlock()");
        abort();
    }
}

void mutexattrInit(pthread_mutexattr_t *attrs) {
    ASSERT(attrs);
    int r = pthread_mutexattr_init(attrs);
    if (r != 0) {
        log_error(r, "Error pthread_mutexattr_init()");
        abort();
    }
}

void mutexattrSettype(pthread_mutexattr_t *attrs, int type) {
    ASSERT(attrs);
    int r = pthread_mutexattr_settype(attrs, type);
    if (r != 0) {
        log_error(r, "Error pthread_mutexattr_settype()");
        abort();
    }
}

void mutexattrDestroy(pthread_mutexattr_t *attrs) {
    ASSERT(attrs);
    int r = pthread_mutexattr_destroy(attrs);
    if (r != 0) {
        log_error(r, "Error pthread_mutexattr_destroy()");
        abort();
    }
}

void conditionInit(pthread_cond_t *cond, pthread_condattr_t *attrs) {
    ASSERT(cond);
    int r = pthread_cond_init(cond, attrs);
    if (r != 0) {
        log_error(r, "Error pthread_cond_init()");
        abort();
    }
}

void conditionDestroy(pthread_cond_t *cond) {
    ASSERT(cond);
    int r = pthread_cond_destroy(cond);
    if (r != 0) {
        log_error(r, "Error pthread_cond_destroy()");
        abort();
    }
}

int conditionWait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    ASSERT(cond);
    int retval;
    retval = pthread_cond_wait(cond, mutex);
    if (retval != 0) {
        log_error(retval, "Error pthread_cond_wait()");
        abort();
    }
    return retval;
}

int conditionTimedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
        const struct timespec *abs_timeout) {
    ASSERT(cond);
    ASSERT(abs_timeout);
    int retval;
    retval = pthread_cond_timedwait(cond, mutex, abs_timeout);
    if (retval != 0 && retval != ETIMEDOUT) {
        log_error(retval, "Error pthread_cond_timedwait()");
        abort();
    }
    return retval;
}

int conditionBroadcast(pthread_cond_t *cond) {
    ASSERT(cond);
    int retval;
    retval = pthread_cond_broadcast(cond);
    if (retval != 0) {
        log_error(retval, "Error pthread_cond_broadcast()");
        abort();
    }
    return retval;
}

int conditionSignal(pthread_cond_t *cond) {
    ASSERT(cond);
    int retval;
    retval = pthread_cond_signal(cond);
    if (retval != 0) {
        log_error(retval, "Error pthread_cond_signal()");
        abort();
    }
    return retval;
}

/*
 * Create a thread executing start_routine with the arguments arg
 * without attributes
 */
pthread_t createThread(void * (*start_routine)(void *), void * arg) {
    ASSERT(start_routine);
    int retval;
    pthread_t thread;
    retval = pthread_create(&thread, NULL, start_routine, arg);
    if (retval != 0) {
        log_error(retval, "Error pthread_create()");
        abort();
    }
    return thread;
}

/*
 * Create a thread executing start_routine with the arguments arg
 * without attributes. Then call pthread_detach.
 */
pthread_t createDetachedThread(void * (*start_routine)(void *), void * arg) {
    int retval;
    pthread_t thread = createThread(start_routine, arg);
    retval = pthread_detach(thread);
    if (retval != 0) {
        log_error(retval, "Error pthread_detach()");
        abort();
    }
    return thread;
}

void joinThread(pthread_t thread, void **value_ptr) {
    int r = pthread_join(thread, value_ptr);
    if (r != 0) {
        log_error(r, "Error pthread_join()");
        abort();
    }
}

#endif /*PTHREAD_WRAP_H_*/
