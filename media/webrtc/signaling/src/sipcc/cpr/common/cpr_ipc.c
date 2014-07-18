/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "cpr.h"
#include "cpr_stdlib.h"
#include "cpr_stdio.h"
#include "plat_api.h"
#include "cpr_string.h"

#ifdef SIP_OS_WINDOWS
#include <windows.h>
#include <process.h>
#include <winuser.h>
#else
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#endif /* SIP_OS_WINDOWS */


#ifdef SIP_OS_WINDOWS
extern cprMsgQueue_t sip_msgq;
extern cprMsgQueue_t gsm_msgq;
extern cprMsgQueue_t tmr_msgq;

extern void gsm_shutdown();
extern void sip_shutdown();

/*
 * Buffer to hold the messages sent/received by CPR. All
 * CPR does is pass four bytes (CNU msg type) and an unsigned
 * four bytes (pointer to the msg buffer).
 */
static char rcvBuffer[100];
#define MSG_BUF 0xF000

#else

#define OS_MSGTQL 31 /* need to check number for MV linux and put here */

/*
 * Internal CPR API
 */
extern pthread_t cprGetThreadId(cprThread_t thread);


/*
 * Extended internal message queue node
 *
 * A double-linked list holding the nessasary message information
 */
typedef struct cpr_msgq_node_s
{
    struct cpr_msgq_node_s *next;
    struct cpr_msgq_node_s *prev;
    void *msg;
    void *pUserData;
} cpr_msgq_node_t;

/*
 * Msg queue information needed to hide OS differences in implementation.
 * To use msg queues, the application code may pass in a name to the
 * create function for msg queues. CPR does not use this field, it is
 * solely for the convenience of the application and to aid in debugging.
 *
 * Note: Statistics are not protected by a mutex; therefore, there exists
 * the possibility that the results may not be accurate.
 *
 * Note:if the depth supplied by OS is insufficient,a message queue owner may
 * increase the message queue depth via cprCreateMessageQueue's depth
 * parameter where the value can range from MSGTQL to CPR_MAX_MSG_Q_DEPTH.
 */
typedef struct cpr_msg_queue_s
{
    struct cpr_msg_queue_s *next;
    const char *name;
    pthread_t thread;
    int32_t queueId;
    uint16_t currentCount;
    uint32_t totalCount;
    uint32_t sendErrors;
    uint32_t reTries;
    uint32_t highAttempts;
    uint32_t selfQErrors;
    uint16_t extendedQDepth;
    uint16_t maxExtendedQDepth;
    pthread_mutex_t mutex;       /* lock for managing extended queue     */
    pthread_cond_t cond;         /* signal for queue/dequeue */
    cpr_msgq_node_t *head;       /* extended queue head (newest element) */
    cpr_msgq_node_t *tail;       /* extended queue tail (oldest element) */
} cpr_msg_queue_t;

/*
 * A enumeration used to report the result of posting a message to
 * a message queue
 */
typedef enum
{
    CPR_MSGQ_POST_SUCCESS,
    CPR_MSGQ_POST_FAILED,
    CPR_MSGQ_POST_PENDING
} cpr_msgq_post_result_e;


/*
 * Head of list of message queues
 */
static cpr_msg_queue_t *msgQueueList = NULL;

/*
 * Mutex to manage message queue list
 */
pthread_mutex_t msgQueueListMutex;

/*
 * CPR_MAX_MSG_Q_DEPTH
 *
 * The maximum queue depth supported by the CPR layer.  This value
 * is arbitrary though the purpose is to limit the memory usage
 * by CPR and avoid (nearly) unbounded situations.
 *
 * Note: This value should be greater than MSGTQL which is currently
 *       defined as 31
 */
#define CPR_MAX_MSG_Q_DEPTH 256

/*
 * CPR_SND_TIMEOUT_WAIT_INTERVAL
 *
 * The interval of time to wait in milliseconds between attempts to
 * send a message to the message queue
 *
 * Note: 20 ms. to avoid less than a tick wake up since on most
 *       OSes 10ms is one 1 tick
 *       this should really be OS_TICK_MS * 2 or OS_TICK_MS + X
 */
#define CPR_SND_TIMEOUT_WAIT_INTERVAL 20

/*
 * CPR_ATTEMPTS_TO_SEND
 *
 * The number of attempts made to send a message when the message
 * would otherwise be blocked.  Note in this condition the thread
 * will sleep the timeout interval to allow the msg queue to be
 * drained.
 *
 * Note: 25 attempts for upto .5 seconds at the interval of
 *       CPR_SND_TIMEOUT_WAIT_INTERVAL worst case.
 */
#define CPR_ATTEMPTS_TO_SEND 25

/*
 * Also, important to note that the total timeout interval must be
 * greater than the SIP's select call timeout value which is 25msec.
 * This is necessary to cover the case where the SIP message queue
 * is full and the select timeout occurs.
 *
 * Total timeout interval = CPR_SND_TIMEOUT_WAIT_INTERVAL *
 *                          CPR_ATTEMPTS_TO_SEND;
 */


/**
 * Peg the statistics for successfully posting a message
 *
 * @param msgq        - message queue
 * @param numAttempts - number of attempts to post message to message queue
 *
 * @return none
 *
 * @pre (msgq not_eq NULL)
 */
static void
cprPegSendMessageStats (cpr_msg_queue_t *msgq, uint16_t numAttempts)
{
    /*
     * Collect statistics
     */
    msgq->totalCount++;

    if (numAttempts > msgq->highAttempts) {
        msgq->highAttempts = numAttempts;
    }
}

/**
 * Post message to system message queue
 *
 * @param msgq       - message queue
 * @param msg        - message to post
 * @param ppUserData - ptr to ptr to option user data
 *
 * @return the post result which is CPR_MSGQ_POST_SUCCESS,
 *         CPR_MSGQ_POST_FAILURE or CPR_MSGQ_POST_PENDING
 *
 * @pre (msgq not_eq NULL)
 * @pre (msg not_eq NULL)
 */
static cpr_msgq_post_result_e
cprPostMessage (cpr_msg_queue_t *msgq, void *msg, void **ppUserData)
{
    cpr_msgq_node_t *node;

    /*
     * Allocate new message queue node
     */
    node = cpr_malloc(sizeof(*node));
    if (!node) {
        errno = ENOMEM;
        return CPR_MSGQ_POST_FAILED;
    }

    pthread_mutex_lock(&msgq->mutex);

    /*
     * Fill in data
     */
    node->msg = msg;
    if (ppUserData != NULL) {
        node->pUserData = *ppUserData;
    } else {
        node->pUserData = NULL;
    }

    /*
     * Push onto list
     */
    node->prev = NULL;
    node->next = msgq->head;
    msgq->head = node;

    if (node->next) {
        node->next->prev = node;
    }

    if (msgq->tail == NULL) {
        msgq->tail = node;
    }
    msgq->currentCount++;

    pthread_cond_signal(&msgq->cond);
    pthread_mutex_unlock(&msgq->mutex);

    return CPR_MSGQ_POST_SUCCESS;

}
#endif /* !SIP_OS_WINDOWS */

/*
 * Functions
 */

/**
 * Creates a message queue
 *
 * @param name  - name of the message queue
 * @param depth - the message queue depth, optional field which will
 *                default if set to zero(0).  This parameter is currently
 *                not supported on Windows.
 *
 * @return Msg queue handle or NULL if init failed, errno provided
 *
 * @note the actual message queue depth will be bounded by the
 *       standard system message queue depth and CPR_MAX_MSG_Q_DEPTH.
 *       If 'depth' is outside of the bounds, the value will be
 *       reset automatically.
 */
cprMsgQueue_t
cprCreateMessageQueue (const char *name, uint16_t depth)
{
    cpr_msg_queue_t *msgq;

#ifndef SIP_OS_WINDOWS
    static int key_id = 100; /* arbitrary starting number */
    pthread_cond_t _cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t _lock = PTHREAD_MUTEX_INITIALIZER;
#endif

    msgq = cpr_calloc(1, sizeof(cpr_msg_queue_t));
    if (msgq == NULL) {
        printf("%s: Malloc failed: %s\n", __FUNCTION__,
               name ? name : "unnamed");
        errno = ENOMEM;
        return NULL;
    }

    msgq->name = name ? name : "unnamed";

#ifndef SIP_OS_WINDOWS
    msgq->queueId = key_id++;
    msgq->cond = _cond;
    msgq->mutex = _lock;

    /*
     * Add message queue to list for statistics reporting
     */
    pthread_mutex_lock(&msgQueueListMutex);
    msgq->next = msgQueueList;
    msgQueueList = msgq;
    pthread_mutex_unlock(&msgQueueListMutex);
#endif /* SIP_OS_WINDOWS */

    return msgq;
}



/**
 * Associate a thread with the message queue
 *
 * @param msgQueue  - msg queue to set
 * @param thread    - CPR thread to associate with queue
 *
 * @return CPR_SUCCESS or CPR_FAILURE
 *
 * @note Nothing is done to prevent overwriting the thread ID
 *       when the value has already been set.
 */
cprRC_t
cprSetMessageQueueThread (cprMsgQueue_t msgQueue, cprThread_t thread)
{
    cpr_msg_queue_t *msgq;

    if ((!msgQueue) || (!thread)) {
        CPR_ERROR("%s: Invalid input\n", __FUNCTION__);
        return CPR_FAILURE;
    }

#ifdef SIP_OS_WINDOWS
    ((cpr_msg_queue_t *)msgQueue)->handlePtr = thread;
#else
    msgq = (cpr_msg_queue_t *) msgQueue;
    if (msgq->thread != 0) {
        CPR_ERROR("%s: over-writing previously msgq thread name for %s",
                  __FUNCTION__, msgq->name);
    }

    msgq->thread = cprGetThreadId(thread);
#endif /* SIP_OS_WINDOWS */

    return CPR_SUCCESS;
}


/**
 * Retrieve a message from a particular message queue
 *
 * @param[in]  msgQueue    - msg queue from which to retrieve the message
 * @param[in]  waitForever - boolean to either wait forever (TRUE) or not
 *                           wait at all (FALSE) if the msg queue is empty.
 * @param[out] ppUserData  - pointer to a pointer to user defined data
 *
 * @return Retrieved message buffer or NULL if failure occurred or
 *         the waitForever flag was set to false and no messages were
 *         on the queue.
 *
 * @note   If ppUserData is defined, the value will be initialized to NULL
 */
void *
cprGetMessage (cprMsgQueue_t msgQueue, boolean waitForever, void **ppUserData)
{
    void *buffer = NULL;

#ifdef SIP_OS_WINDOWS
    struct msgbuffer *rcvMsg = (struct msgbuffer *)rcvBuffer;
    cpr_msg_queue_t *pCprMsgQueue;
    MSG msg;
    cpr_thread_t *pThreadPtr;
#else
    cpr_msg_queue_t *msgq = (cpr_msg_queue_t *) msgQueue;
    cpr_msgq_node_t *node;
    struct timespec timeout;
    struct timeval tv;
    struct timezone tz;
#endif

    if (!msgQueue) {
        CPR_ERROR("%s - invalid msgQueue\n", __FUNCTION__);
        return NULL;
    }

    /* Initialize ppUserData */
    if (ppUserData) {
        *ppUserData = NULL;
    }

#ifdef SIP_OS_WINDOWS
    pCprMsgQueue = (cpr_msg_queue_t *)msgQueue;
    memset(&msg, 0, sizeof(MSG));

    if (waitForever == TRUE) {
        if (GetMessage(&msg, NULL, 0, 0) == -1) {
            CPR_ERROR("%s - msgQueue = %x failed: %d\n",
                      __FUNCTION__, msgQueue, GetLastError());
            return NULL;
        }
    } else {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) == 0) {
            /* no message present */
            return NULL;
        }
    }

    switch (msg.message) {
        case WM_CLOSE:
            if (msgQueue == &gsm_msgq)
            {
                CPR_ERROR("%s - WM_CLOSE GSM msg queue\n", __FUNCTION__);
                gsm_shutdown();
            }
            else if (msgQueue == &sip_msgq)
            {
                CPR_ERROR("%s - WM_CLOSE SIP msg queue\n", __FUNCTION__);
                sip_regmgr_destroy_cc_conns();
                sip_shutdown();
            }
            else if (msgQueue == &tmr_msgq)
            {
                CPR_ERROR("%s - WM_CLOSE TMR msg queue\n", __FUNCTION__);
            }

            pThreadPtr=(cpr_thread_t *)pCprMsgQueue->handlePtr;
            if (pThreadPtr)
            {
                CloseHandle(pThreadPtr->u.handlePtr);
            }
            /* zap the thread ptr, since the thread is going away now */
            pCprMsgQueue->handlePtr = NULL;
            _endthreadex(0);
            break;
        case MSG_BUF:
            rcvMsg = (struct msgbuffer *)msg.wParam;
            buffer = rcvMsg->msgPtr;
            if (ppUserData) {
                *ppUserData = rcvMsg->usrPtr;
            }
            cpr_free((void *)msg.wParam);
            break;
        case MSG_ECHO_EVENT:
            {
                HANDLE event;
                event = (HANDLE*)msg.wParam;
                SetEvent( event );
            }
            break;
        case WM_TIMER:
            DispatchMessage(&msg);
            return NULL;
            break;
        default:
            break;
    }
#else
    /*
     * If waitForever is set, block on the message queue
     * until a message is received, else return after
     * 25msec of waiting
     */
    pthread_mutex_lock(&msgq->mutex);

    if (!waitForever)
    {
        // We'll wait till 25uSec from now
        gettimeofday(&tv, &tz);
        timeout.tv_nsec = (tv.tv_usec * 1000) + 25000;
        timeout.tv_sec = tv.tv_sec;

        pthread_cond_timedwait(&msgq->cond, &msgq->mutex, &timeout);
    }
    else
    {
        while(msgq->tail==NULL)
        {
            pthread_cond_wait(&msgq->cond, &msgq->mutex);
        }
    }

    // If there is a message on the queue, de-queue it
    if (msgq->tail)
    {
        node = msgq->tail;
        msgq->tail = node->prev;
        if (msgq->tail) {
            msgq->tail->next = NULL;
        }
        if (msgq->head == node) {
            msgq->head = NULL;
        }
        msgq->currentCount--;
        /*
         * Pull out the data
         */
        if (ppUserData) {
            *ppUserData = node->pUserData;
        }
        buffer = node->msg;

    }

    pthread_mutex_unlock(&msgq->mutex);
#endif /* SIP_OS_WINDOWS */

    return buffer;
}


/**
 * Place a message on a particular queue.  Note that caller may
 * block (see comments below)
 *
 * @param msgQueue   - msg queue on which to place the message
 * @param msg        - pointer to the msg to place on the queue
 * @param ppUserData - pointer to a pointer to user defined data
 *
 * @return CPR_SUCCESS or CPR_FAILURE, errno provided
 *
 * @note 1. Messages queues are set to be non-blocking, those cases
 *       where the system call fails with a would-block error code
 *       (EAGAIN) the function will attempt other mechanisms described
 *       below.
 * @note 2. If enabled with an extended message queue, either via a
 *       call to cprCreateMessageQueue with depth value or a call to
 *       cprSetExtendMessageQueueDepth() (when unit testing), the message
 *       will be added to the extended message queue and the call will
 *       return successfully.  When room becomes available on the
 *       system's message queue, those messages will be added.
 * @note 3. If the message queue becomes full and no space is availabe
 *       on the extended message queue, then the function will attempt
 *       to resend the message up to CPR_ATTEMPTS_TO_SEND and the
 *       calling thread will *BLOCK* CPR_SND_TIMEOUT_WAIT_INTERVAL
 *       milliseconds after each failed attempt.  If unsuccessful
 *       after all attempts then EGAIN error code is returned.
 * @note 4. This applies to all CPR threads, including the timer thread.
 *       So it is possible that the timer thread would be forced to
 *       sleep which would have the effect of delaying all active
 *       timers.  The work to fix this rare situation is not considered
 *       worth the effort to fix....so just leaving as is.
 */
cprRC_t
cprSendMessage (cprMsgQueue_t msgQueue, void *msg, void **ppUserData)
{
#ifdef SIP_OS_WINDOWS
    struct msgbuffer *sendMsg;
    cpr_thread_t *pCprThread;
    HANDLE *hThread;
#else
    static const char error_str[] = "%s: Msg not sent to %s queue: %s\n";
    cpr_msgq_post_result_e rc;
    cpr_msg_queue_t *msgq = (cpr_msg_queue_t *) msgQueue;
    int16_t attemptsToSend = CPR_ATTEMPTS_TO_SEND;
    uint16_t numAttempts   = 0;
#endif

    if (!msgQueue) {
        CPR_ERROR("%s - msgQueue is NULL\n", __FUNCTION__);
        return CPR_FAILURE;
    }

#ifdef SIP_OS_WINDOWS
    pCprThread = (cpr_thread_t *)(((cpr_msg_queue_t *)msgQueue)->handlePtr);
    if (!pCprThread) {
        CPR_ERROR("%s - msgQueue(%x) not associated with a thread\n",
                  __FUNCTION__, msgQueue);
        return CPR_FAILURE;
    }

    hThread = (HANDLE*)(pCprThread->u.handlePtr);
    if (!hThread) {
        CPR_ERROR("%s - msgQueue(%x)'s thread(%x) not assoc. with Windows\n",
                __FUNCTION__, msgQueue, pCprThread);
        return CPR_FAILURE;
    }

    /* Package up the message */
    sendMsg = (struct msgbuffer *)cpr_calloc(1, sizeof(struct msgbuffer));
    if (!sendMsg) {
        CPR_ERROR("%s - No memory\n", __FUNCTION__);
        return CPR_FAILURE;
    }
    sendMsg->mtype = PHONE_IPC_MSG;

    /* Save the address of the message */
    sendMsg->msgPtr = msg;

    /* Allow the ppUserData to be optional */
    if (ppUserData) {
        sendMsg->usrPtr = *ppUserData;
    }

    /* Post the message */
    if (hThread == NULL || PostThreadMessage(pCprThread->threadId, MSG_BUF,
        (WPARAM)sendMsg, 0) == 0 ) {
        CPR_ERROR("%s - Msg not sent: %d\n", __FUNCTION__, GetLastError());
        cpr_free(sendMsg);
        return CPR_FAILURE;
    }
    return CPR_SUCCESS;

#else
    /*
     * Attempt to send message
     */
    do {

        /*
         * Post the message to the Queue
         */
        rc = cprPostMessage(msgq, msg, ppUserData);

        if (rc == CPR_MSGQ_POST_SUCCESS) {
            cprPegSendMessageStats(msgq, numAttempts);
            return CPR_SUCCESS;
        } else if (rc == CPR_MSGQ_POST_FAILED) {
            CPR_ERROR("%s: Msg not sent to %s queue: %d\n",
                      __FUNCTION__, msgq->name, errno);
            msgq->sendErrors++;
            /*
             * If posting to calling thread's own queue,
             * then peg the self queue error.
             */
            if (pthread_self() == msgq->thread) {
                msgq->selfQErrors++;
            }

            return CPR_FAILURE;
        }


        /*
         * Did not succeed in sending the message, so continue
         * to attempt up to the CPR_ATTEMPTS_TO_SEND.
         */
        attemptsToSend--;
        if (attemptsToSend > 0) {
            /*
             * Force a context-switch of the thread attempting to
             * send the message, in order to help the case where
             * the msg queue is full and the owning thread may get
             * a a chance be scheduled so it can drain it (Note:
             * no guarantees, more of a "last-ditch effort" to
             * recover...especially when temporarily over-whelmed).
             */
            cprSleep(CPR_SND_TIMEOUT_WAIT_INTERVAL);
            msgq->reTries++;
            numAttempts++;
        }
    } while (attemptsToSend > 0);

    CPR_ERROR(error_str, __FUNCTION__, msgq->name, "FULL");
    msgq->sendErrors++;
    return CPR_FAILURE;
#endif /* SIP_OS_WINDOWS */
}



/**
 * cprGetDepth
 *
 * @brief get depth of a message queue
 *
 * The pSIPCC uses this API to look at the depth of a message queue for internal
 * routing and throttling decision
 *
 * @param[in] msgQueue - message queue
 *
 * @return depth of msgQueue
 *
 * @pre (msgQueue not_eq NULL)
 */
uint16_t cprGetDepth (cprMsgQueue_t msgQueue)
{
#ifdef SIP_OS_WINDOWS
    return 0;
#else
    cpr_msg_queue_t *msgq;
    msgq = (cpr_msg_queue_t *) msgQueue;
    return msgq->currentCount;
#endif /* SIP_OS_WINDOWS */
}


