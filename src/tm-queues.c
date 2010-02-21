#include "suricata.h"
#include "threads.h"
#include "tm-queues.h"
#include "util-debug.h"

#define TMQ_MAX_QUEUES 256

static uint16_t tmq_id = 0;
static Tmq tmqs[TMQ_MAX_QUEUES];

Tmq* TmqAlloc(void) {
    Tmq *q = malloc(sizeof(Tmq));
    if (q == NULL)
        goto error;

    memset(q, 0, sizeof(Tmq));
    return q;

error:
    return NULL;
}

Tmq* TmqCreateQueue(char *name) {
    if (tmq_id >= TMQ_MAX_QUEUES)
        goto error;

    Tmq *q = &tmqs[tmq_id];
    q->name = name;
    q->id = tmq_id++;

    SCLogDebug("created queue \'%s\', %p", name, q);
    return q;

error:
    return NULL;
}

Tmq* TmqGetQueueByName(char *name) {
    uint16_t i;

    for (i = 0; i < tmq_id; i++) {
        if (strcmp(tmqs[i].name, name) == 0)
            return &tmqs[i];
    }

    return NULL;
}

void TmqDebugList(void) {
    uint16_t i = 0;
    for (i = 0; i < tmq_id; i++) {
        /* get a lock accessing the len */
        SCMutexLock(&trans_q[tmqs[i].id].mutex_q);
        printf("TmqDebugList: id %" PRIu32 ", name \'%s\', len %" PRIu32 "\n", tmqs[i].id, tmqs[i].name, trans_q[tmqs[i].id].len);
        SCMutexUnlock(&trans_q[tmqs[i].id].mutex_q);
    }
}

void TmqResetQueues(void) {
    memset(&tmqs, 0x00, sizeof(tmqs));
    tmq_id = 0;
}

/**
 * \brief Checks if all the queues allocated so far have at least one reader
 *        and writer.
 */
void TmValidateQueueState(void)
{
    int i = 0;
    char err = FALSE;

    for (i = 0; i < tmq_id; i++) {
        SCMutexLock(&trans_q[tmqs[i].id].mutex_q);
        if (tmqs[i].reader_cnt == 0) {
            printf("Error: Queue \"%s\" doesn't have a reader\n", tmqs[i].name);
            err = TRUE;
        } else if (tmqs[i].writer_cnt == 0) {
            printf("Error: Queue \"%s\" doesn't have a writer\n", tmqs[i].name);
            err = TRUE;
        }
        SCMutexUnlock(&trans_q[tmqs[i].id].mutex_q);

        if (err == TRUE)
            goto error;
    }

    return;

error:
    exit(EXIT_FAILURE);
}
