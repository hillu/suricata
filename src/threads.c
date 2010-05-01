/* Copyright (c) 2009 Open Information Security Foundation */

/**
 *
 * \file
 *
 * \author Victor Julien <victor@inliniac.net>
 * \author Pablo Rincon Crespo <pablo.rincon.crespo@gmail.com>
 */

#include "suricata-common.h"
#include "util-unittest.h"
#include "debug.h"
#include "util-debug.h"
#include "util-unittest.h"
#include "threads.h"

#ifdef UNITTESTS /* UNIT TESTS */

/**
 * \brief Test Mutex macros
 */
int ThreadMacrosTest01Mutex(void) {
    SCMutex mut;
    int r = 0;
    r |= SCMutexInit(&mut, NULL);
    r |= SCMutexLock(&mut);
    r |= (SCMutexTrylock(&mut) == EBUSY)? 0 : 1;
    r |= SCMutexUnlock(&mut);
    r |= SCMutexDestroy(&mut);

    return (r == 0)? 1 : 0;
}

/**
 * \brief Test Spin Macros
 *
 * Valgrind's DRD tool (valgrind-3.5.0-Debian) reports:
 *
 * ==31156== Recursive locking not allowed: mutex 0x7fefff97c, recursion count 1, owner 1.
 * ==31156==    at 0x4C2C77E: pthread_spin_trylock (drd_pthread_intercepts.c:829)
 * ==31156==    by 0x40EB3E: ThreadMacrosTest02Spinlocks (threads.c:40)
 * ==31156==    by 0x532E8A: UtRunTests (util-unittest.c:182)
 * ==31156==    by 0x4065C3: main (suricata.c:789)
 *
 * To me this is a false possitve, as the whole point of "trylock" is to see
 * if a spinlock is actually locked.
 *
 */
int ThreadMacrosTest02Spinlocks(void) {
    SCSpinlock mut;
    int r = 0;
    r |= SCSpinInit(&mut, 0);
    r |= SCSpinLock(&mut);
    r |= (SCSpinTrylock(&mut) == EBUSY)? 0 : 1;
    r |= SCSpinUnlock(&mut);
    r |= SCSpinDestroy(&mut);

    return (r == 0)? 1 : 0;
}

#endif /* UNIT TESTS */

/**
 * \brief this function registers unit tests for DetectId
 */
void ThreadMacrosRegisterTests(void)
{
#ifdef UNITTESTS /* UNIT TESTS */
    UtRegisterTest("ThreadMacrosTest01Mutex", ThreadMacrosTest01Mutex, 1);
    UtRegisterTest("ThreadMacrossTest02Spinlocks", ThreadMacrosTest02Spinlocks, 1);
#endif /* UNIT TESTS */
}
