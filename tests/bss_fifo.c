/*
 * OpenSSL BIO FIFO
 *
 * Copyright (C) 2008-2011 Florent Bondoux
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

//#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

//#include "log.h"
#include "bss_fifo.h"
#include "pthread_wrap.h"

/*
 * Internal functions
 */
static int fifo_write(BIO *h, const char *buf, int num);
static int fifo_read(BIO *h, char *buf, int size);
static long fifo_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int fifo_new(BIO *h);
static int fifo_free(BIO *data);
static int fifo_allocate(BIO *bi, int len, int data_size);

/* BIO_METHOD structure describing the BIO */
static BIO_METHOD fifo_method = { BIO_TYPE_FIFO, // type
        "fifo buffer", // name
        fifo_write, // write function
        fifo_read, // read function
        NULL, // puts function, not implemented
        NULL, // gets function, not implemented
        fifo_ctrl, // BIO control function
        fifo_new, // creation
        fifo_free, // free
        NULL, // callback function, not used
        };

/*
 * Create a new FIFO BIO.
 * len: number of items in the queue
 * data_size: packet size
 */
BIO *BIO_new_fifo(int len, int data_size) {
    BIO *bi;
    int r;
    bi = BIO_new(&fifo_method);
    if (bi == NULL) {
        return NULL;
    }
    r = fifo_allocate(bi, len, data_size);
    if (r != 1) {
        BIO_free(bi);
        return NULL;
    }
    return bi;
}

/*
 * Allocate everything in the BIO and set bi->init = 1
 * len: number of items in the queue
 * data_size: packet size
 */
int fifo_allocate(BIO *bi, int len, int data_size) {
    unsigned int i, j;
    struct fifo_data * d;

    bi->ptr = malloc(sizeof(struct fifo_data));
    if (bi->ptr == NULL) {
        //log_error(errno, "Cannot allocate a new client");
        return 0;
    }
    d = (struct fifo_data *) bi->ptr;
    d->index_read = 0;
    d->index_write = 0;
    d->size = len;
    d->threshold = len/10;
    d->rcv_timer_exp = 0;
    d->rcv_timeout.tv_sec = d->rcv_timeout.tv_usec = 0;
    d->next_rcv_timeout.tv_sec = d->next_rcv_timeout.tv_usec = 0;
    d->curr_rcv_timeout.tv_sec = d->curr_rcv_timeout.tv_usec = 0;
    d->droptail = 0;

    d->fifo = (struct fifo_item *) malloc(d->size * sizeof(struct fifo_item));
    if (d->fifo == NULL) {
        free(bi->ptr);
        //log_error(errno, "Cannot allocate a new client");
        return 0;
    }

    for (i = 0; i < d->size; i++) {
        d->fifo[i].data = malloc(data_size);
        d->fifo[i].data_size = data_size;
        if (d->fifo[i].data == NULL) {
            break;
        }
    }
    if (i != d->size) {
        //log_error(errno, "Cannot allocate a new client");
        for (j = 0; j < i; j++) {
            free(d->fifo[j].data);
        }
        free(d->fifo);
        free(bi->ptr);
        return 0;
    }

    conditionInit(&d->cond_read, NULL);
    conditionInit(&d->cond_write, NULL);
    mutexInit(&d->mutex, NULL);
    d->nelem = 0;
    d->waiting_read = 0;
    d->waiting_write = 0;

    bi->init = 1;

    return 1;
}

/*
 * Doesn't do much
 * the work is done by fifo_allocate
 */
static int fifo_new(BIO *bi) {
    bi->shutdown = 1; // the "close flag" (see BIO_set_close(3))
    bi->init = 0;
    bi->num = -1; // not used

    return 1;
}

/*
 * free fifo_data and fifo_item structures
 */
static int fifo_free(BIO *bi) {
    struct fifo_data *d;
    unsigned int i;
    if (bi == NULL)
        return 0; // we have to check
    if (bi->shutdown) {
        if ((bi->init) && (bi->ptr != NULL)) {
            d = (struct fifo_data *) bi->ptr;
            for (i = 0; i < d->size; i++) {
                free(d->fifo[i].data);
            }
            free(d->fifo);
            mutexDestroy(&d->mutex);
            conditionDestroy(&d->cond_read);
            conditionDestroy(&d->cond_write);
            free(d);
            bi->ptr = NULL;
        }
    }
    return 1;
}

/* define tspec for use with sem_timedwait or pthread_cond_timedwait */
static inline void set_timeout(struct timespec *tspec, struct timeval timeout) {
    struct timeval now;
    gettimeofday(&now, NULL);
    tspec->tv_nsec = (now.tv_usec + timeout.tv_usec) * 1000L;
    tspec->tv_sec = now.tv_sec + timeout.tv_sec;
    if (tspec->tv_nsec >= 1000000000L) { // overflow
        tspec->tv_nsec -= 1000000000L;
        tspec->tv_sec ++;
    }
}

/* The handling of the receiving timeout is the same than in the DGRAM Bio.
 * This function is an adaptation of the original in bss_dgram.c.
 *
 * The resulting receiving timeout is un curr_rcv_timeout.
 * The original timeout is stored in rcv_timeout.
 */
static void fifo_adjust_rcv_timeout(BIO *b) {
    struct fifo_data *d;

    d = (struct fifo_data *) b->ptr;

    if (d->next_rcv_timeout.tv_sec > 0 || d->next_rcv_timeout.tv_usec > 0) {
        struct timeval timenow, timeleft;

        /* Read current socket timeout */
        d->rcv_timeout = d->curr_rcv_timeout;

        /* Get current time */
        gettimeofday(&timenow, NULL);

        /* Calculate time left until timer expires */
        memcpy(&timeleft, &(d->next_rcv_timeout), sizeof(struct timeval));
        timeleft.tv_sec -= timenow.tv_sec;
        timeleft.tv_usec -= timenow.tv_usec;
        if (timeleft.tv_usec < 0) {
            timeleft.tv_sec--;
            timeleft.tv_usec += 1000000;
        }

        if (timeleft.tv_sec < 0) {
            timeleft.tv_sec = 0;
            timeleft.tv_usec = 1;
        }

        /* Adjust socket timeout if next handhake message timer
         * will expire earlier.
         */
        if ((d->rcv_timeout.tv_sec == 0 && d->rcv_timeout.tv_usec
                == 0) || (d->rcv_timeout.tv_sec > timeleft.tv_sec)
                || (d->rcv_timeout.tv_sec == timeleft.tv_sec
                        && d->rcv_timeout.tv_usec >= timeleft.tv_usec)) {
            d->curr_rcv_timeout = timeleft;
        }
    }
}

/*
 * Adaptation of bss_dgram.c
 * Restore the original receiving timeout if it was changed by
 * fifo_adjust_rcv_timeout.
 */
static void fifo_reset_rcv_timeout(BIO *b) {
    struct fifo_data *d;

    d = (struct fifo_data *) b->ptr;

    /* Is a timer active? */
    if (d->next_rcv_timeout.tv_sec > 0 || d->next_rcv_timeout.tv_usec > 0) {
        d->curr_rcv_timeout = d->rcv_timeout;
    }
}

/*
 * Blocking read from the FIFO
 */
static int fifo_read(BIO *b, char *out, int outl) {
    int ret = 0, len, r = 0;
    struct fifo_data *d;
    struct fifo_item *item;
    struct timespec timeout;

    d = (struct fifo_data *) b->ptr;

    if (!out) {
        return ret;
    }

    mutexLock(&d->mutex);
    if (d->nelem == 0) {
        d->waiting_read++;
        fifo_adjust_rcv_timeout(b);
        if (d->curr_rcv_timeout.tv_sec || d->curr_rcv_timeout.tv_usec) {
            set_timeout(&timeout, d->curr_rcv_timeout);
            r = conditionTimedwait(&d->cond_read, &d->mutex, &timeout);
        }
        else {
            conditionWait(&d->cond_read, &d->mutex);
        }
        fifo_reset_rcv_timeout(b);
        d->waiting_read--;
    }


    BIO_clear_retry_flags(b);

    if (r != 0) { // timeout
        BIO_set_retry_read(b);
        d->rcv_timer_exp = 1;
        ret = -1;
    }
    else {
        item = &d->fifo[d->index_read];
        len = item->size;
        ret = (outl >= len) ? len : outl; // is "out" big enough to store the packet
        if ((out != NULL)) {
            memcpy(out, item->data, ret); // copy the data into "out" and update the queue
        }
        (d->index_read == d->size - 1) ? d->index_read = 0 : d->index_read++;
        d->nelem--;
        if (d->waiting_write && (d->size - d->nelem) >= d->threshold) {
            conditionSignal(&d->cond_write);
        }
    }
    mutexUnlock(&d->mutex);
    return ret;
}

/*
 * Blocking write to the FIFO
 */
static int fifo_write(BIO *b, const char *in, int inl) {
    struct fifo_data *d;
    struct fifo_item *item;
    int ret = -1;

    if (in == NULL) {
        BIOerr(BIO_F_BIO_WRITE,BIO_R_NULL_PARAMETER);
        return ret;
    }

    d = (struct fifo_data *) b->ptr;
    mutexLock(&d->mutex);
    if (d->nelem == d->size && d->droptail) {
        mutexUnlock(&d->mutex);
        return inl;
    }
    while (d->nelem == d->size) {
        d->waiting_write++;
        conditionWait(&d->cond_write, &d->mutex);
        d->waiting_write--;
    }

    BIO_clear_retry_flags(b);
    item = &d->fifo[d->index_write];
    if (inl > item->data_size) {
        void * new = malloc(inl);
        if (new) {
            free(item->data);
            item->data = new;
            item->data_size = inl;
        }
        else {
            inl = item->data_size;
        }
    }
    item->size = inl;
    memcpy(item->data, in, inl);
    (d->index_write == d->size - 1) ? d->index_write = 0 : d->index_write++;
    ret = inl;
    d->nelem++;
    if (d->waiting_read) {
        conditionSignal(&d->cond_read);
    }
    mutexUnlock(&d->mutex);
    return ret;
}

/*
 * All the controls options are not implemented
 */
static long fifo_ctrl(BIO *b, int cmd, long num, void *ptr) {
    long ret = 1;
    unsigned int i;
    int v;
    struct fifo_item *item;

    struct fifo_data * d = (struct fifo_data *) b->ptr;

    switch (cmd) {
        case BIO_CTRL_RESET:
            d->index_read = 0;
            d->index_write = 0;
            d->nelem = 0;
            break;
        case BIO_CTRL_EOF:
            mutexLock(&d->mutex);
            ret = (long) (d->nelem == 0);
            mutexUnlock(&d->mutex);
            break;
        case BIO_CTRL_GET_CLOSE:
            ret = (long) b->shutdown;
            break;
        case BIO_CTRL_SET_CLOSE:
            b->shutdown = (int) num;
            break;

        case BIO_CTRL_WPENDING:
            ret = 0L;
            break;
        case BIO_CTRL_PENDING:
            ret = 0;
            mutexLock(&d->mutex);
            v = d->nelem;
            for (i = d->index_read; i < d->index_read + v; i++) {
                item = &d->fifo[((i < d->size) ? i : i - d->size)];
                ret += item->size;
            }
            mutexUnlock(&d->mutex);
            break;
        case BIO_CTRL_DUP:
        case BIO_CTRL_FLUSH:
            ret = 1;
            break;
        case BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT:
            d->next_rcv_timeout = *(struct timeval *) ptr;
            break;
        case BIO_CTRL_DGRAM_SET_RECV_TIMEOUT:
            d->curr_rcv_timeout = *(struct timeval *) ptr;
            break;
        case BIO_CTRL_DGRAM_GET_RECV_TIMEOUT:
            *(struct timeval *)ptr = d->curr_rcv_timeout;
            break;
        case BIO_CTRL_DGRAM_GET_RECV_TIMER_EXP:
            if (d->rcv_timer_exp) {
                ret = 1;
                d->rcv_timer_exp = 0;
            }
            else
                ret = 0;
            break;
        case BIO_CTRL_FIFO_SET_DROPTAIL:
            mutexLock(&d->mutex);
            d->droptail = (int) num;
            mutexUnlock(&d->mutex);
            break;
        case BIO_CTRL_FIFO_GET_DROPTAIL:
            mutexLock(&d->mutex);
            ret = d->droptail;
            mutexUnlock(&d->mutex);
            break;
        case BIO_CTRL_PUSH:
        case BIO_CTRL_POP:
        default:
            ret = 0;
            break;
    }
    return ret;
}
