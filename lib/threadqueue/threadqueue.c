#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <stdio.h>

#ifndef WIN32
#include <pthread.h>
#else
#include <windows.h>
#endif

#include "threadqueue.h"


#define MSGPOOL_SIZE 512

struct msglist {
    struct threadmsg msg;
    struct msglist *next;
};

static inline struct msglist *get_msglist(struct threadqueue *queue)
{
    struct msglist *tmp;

    if(queue->msgpool != NULL) {
        tmp = queue->msgpool;
        queue->msgpool = tmp->next;
        queue->msgpool_length--;
    } else {
        tmp = malloc(sizeof *tmp);
    }

    return tmp;
}

static inline void release_msglist(struct threadqueue *queue,
                                   struct msglist *node)
{

    if(queue->msgpool_length > ( queue->length/8 + MSGPOOL_SIZE)) {
        free(node);
    } else {
        node->msg.data = NULL;
        node->msg.msgtype = 0;
        node->next = queue->msgpool;
        queue->msgpool = node;
        queue->msgpool_length++;
    }
    if(queue->msgpool_length > (queue->length/4 + MSGPOOL_SIZE*10)) {
        struct msglist *tmp = queue->msgpool;
        queue->msgpool = tmp->next;
        free(tmp);
        queue->msgpool_length--;
    }
}

int thread_queue_init(struct threadqueue *queue)
{
#ifndef WIN32
    int ret = 0;
#endif
    if (queue == NULL) {
        return EINVAL;
    }
    memset(queue, 0, sizeof(struct threadqueue));
#ifndef WIN32
    ret = pthread_cond_init(&queue->cond, NULL);
    if (ret != 0) {
        return ret;
    }

    ret = pthread_mutex_init(&queue->mutex, NULL);
    if (ret != 0) {
        pthread_cond_destroy(&queue->cond);
        return ret;
    }
#else
    queue->cond = CreateEvent(NULL, TRUE, FALSE, TEXT("WriteEvent"));
    if (queue->cond == NULL) {
        return -1;
    }

    queue->mutex = CreateMutex(NULL, FALSE, NULL);
    if (queue->mutex == NULL) {
        return -1;
    }
#endif

    return 0;

}

int thread_queue_add(struct threadqueue *queue, void *data, long msgtype)
{
    struct msglist *newmsg;
#ifndef WIN32
    pthread_mutex_lock(&queue->mutex);
#else
    // This function returns a DWORD but we don't use it
    WaitForSingleObject(queue->mutex, INFINITE);
#endif
    newmsg = get_msglist(queue);
    if (newmsg == NULL) {
#ifndef WIN32
        pthread_mutex_unlock(&queue->mutex);
#else
        ReleaseMutex(queue->mutex);
#endif
        return ENOMEM;
    }
    newmsg->msg.data = data;
    newmsg->msg.msgtype = msgtype;

    newmsg->next = NULL;
    if (queue->last == NULL) {
        queue->last = newmsg;
        queue->first = newmsg;
    } else {
        queue->last->next = newmsg;
        queue->last = newmsg;
    }

        if(queue->length == 0)
#ifndef WIN32
                pthread_cond_broadcast(&queue->cond);
#else
                SetEvent(queue->cond);
#endif
    queue->length++;
#ifndef WIN32
    pthread_mutex_unlock(&queue->mutex);
#else
    ReleaseMutex(queue->mutex);
#endif
    return 0;

}

int thread_queue_get(struct threadqueue *queue, const struct timespec *timeout,
struct threadmsg *msg)
{
    struct msglist *firstrec;
#ifndef WIN32
    int ret = 0;
#endif
    struct timespec abstimeout;

    if (queue == NULL || msg == NULL) {
        return EINVAL;
    }
    if (timeout) {
        struct timeval now;

        gettimeofday(&now, NULL);
        abstimeout.tv_sec = now.tv_sec + timeout->tv_sec;
        abstimeout.tv_nsec = (now.tv_usec * 1000) + timeout->tv_nsec;
        if (abstimeout.tv_nsec >= 1000000000) {
            abstimeout.tv_sec++;
            abstimeout.tv_nsec -= 1000000000;
        }
    }

#ifndef WIN32
    pthread_mutex_lock(&queue->mutex);
#else
    WaitForSingleObject(queue->mutex, INFINITE);
#endif

    /* Will wait until awakened by a signal or broadcast */
    //Need to loop to handle spurious wakeups
#ifndef WIN32
    while (queue->first == NULL && ret != ETIMEDOUT) {  
        if (timeout) {
            ret = pthread_cond_timedwait(&queue->cond, &queue->mutex,
                                         &abstimeout);
        } else {
            pthread_cond_wait(&queue->cond, &queue->mutex);

        }
    }
    if (ret == ETIMEDOUT) {
        pthread_mutex_unlock(&queue->mutex);
        return ret;
    }
#else
    while (queue->first == NULL) {  
        WaitForSingleObject(queue->cond, INFINITE);
    }
#endif

    firstrec = queue->first;
    queue->first = queue->first->next;
    queue->length--;

    if (queue->first == NULL) {
        queue->last = NULL;     // we know this since we hold the lock
        queue->length = 0;
    }


    msg->data = firstrec->msg.data;
    msg->msgtype = firstrec->msg.msgtype;
        msg->qlength = queue->length;

    release_msglist(queue,firstrec);
#ifndef WIN32
    pthread_mutex_unlock(&queue->mutex);
#else
    ReleaseMutex(queue->mutex);
#endif

    return 0;
}

//maybe caller should supply a callback for cleaning the elements ?
int thread_queue_cleanup(struct threadqueue *queue, int freedata)
{
    struct msglist *rec;
    struct msglist *next;
    struct msglist *recs[2];
    int ret,i;
    if (queue == NULL) {
        return EINVAL;
    }

#ifndef WIN32
    pthread_mutex_lock(&queue->mutex);
#else
    WaitForSingleObject(queue->mutex, INFINITE);
#endif
    recs[0] = queue->first;
    recs[1] = queue->msgpool;
    for(i = 0; i < 2 ; i++) {
        rec = recs[i];
        while (rec) {
            next = rec->next;
            if (freedata) {
                free(rec->msg.data);
            }
            free(rec);
            rec = next;
        }
    }

#ifndef WIN32
    pthread_mutex_unlock(&queue->mutex);
    ret = pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
#else
    ReleaseMutex(queue->mutex);
    CloseHandle(queue->mutex);
    CloseHandle(queue->cond);
#endif

    return ret;

}

long thread_queue_length(struct threadqueue *queue)
{
    long counter;
    // get the length properly
#ifndef WIN32
    pthread_mutex_lock(&queue->mutex);
#else
    WaitForSingleObject(queue->mutex, INFINITE);
#endif
    counter = queue->length;
#ifndef WIN32
    pthread_mutex_unlock(&queue->mutex);
#else
    ReleaseMutex(queue->mutex);
#endif
    return counter;

}

// TODO - Implement using nanosleep
int thread_queue_bput(struct threadqueue *queue, const void *data, size_t len)
{
    int retval = 0;
#ifndef WIN32
    struct timespec req, rem;
    req.tv_sec = 0;
    req.tv_nsec = 1000;
#endif

    void *queue_data = malloc(len);
    memcpy(queue_data, data, len);

    while (1) {
        retval = thread_queue_add(queue, queue_data, len);
#ifndef WIN32
        if (retval == ENOMEM) nanosleep(&req, &rem);
#else
        if (retval == ENOMEM) Sleep(1);
#endif
        else break;
    }
    return retval;
}

int thread_queue_bget(struct threadqueue *queue, void *buf, size_t len)
{
    int retval;
    struct threadmsg msg;
    retval = thread_queue_get(queue, NULL, &msg);
    if (retval == 0) {
        // msgtype is used to store data length
        if (msg.msgtype > len) return -1;
        len = msg.msgtype;
        memcpy(buf, msg.data, len);
        free(msg.data);
        retval = len;
    }
    return retval;
}
