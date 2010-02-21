/** Copyright (c) 2009 Open Information Security Foundation.
 *  \author Anoop Saldanha <poonaatsoc@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pcre.h>

#include "suricata-common.h"
#include "detect.h"
#include "detect-flowbits.h"
#include "detect-engine-sigorder.h"
#include "detect-pcre.h"

#include "util-unittest.h"
#include "util-debug.h"

#define DETECT_FLOWVAR_NOT_USED   1
#define DETECT_FLOWVAR_TYPE_READ  2
#define DETECT_FLOWVAR_TYPE_SET   3

#define DETECT_PKTVAR_NOT_USED   1
#define DETECT_PKTVAR_TYPE_READ  2
#define DETECT_PKTVAR_TYPE_SET   3


/**
 * \brief Registers a keyword-based, signature ordering function
 *
 * \param de_ctx  Pointer to the detection engine context from which the
 *                signatures have to be ordered.
 * \param FuncPtr Pointer to the signature ordering function.  The prototype of
 *                the signature ordering function should accept a pointer to a
 *                SCSigSignatureWrapper as its argument and shouldn't return
 *                anything
 */
static void SCSigRegisterSignatureOrderingFunc(DetectEngineCtx *de_ctx,
                                               void (*FuncPtr)(DetectEngineCtx *de_ctx, SCSigSignatureWrapper *))
{
    SCSigOrderFunc *curr = NULL;
    SCSigOrderFunc *prev = NULL;
    SCSigOrderFunc *temp = NULL;

    curr = de_ctx->sc_sig_order_funcs;
    prev = curr;
    while (curr != NULL) {
        prev = curr;
        if (curr->FuncPtr == FuncPtr)
            break;

        curr = curr->next;
    }

    if (curr != NULL)
        return;

    if ( (temp = malloc(sizeof(SCSigOrderFunc))) == NULL) {
        printf("Error allocating memory\n");
        exit(EXIT_FAILURE);
    }
    memset(temp, 0, sizeof(SCSigOrderFunc));

    temp->FuncPtr = FuncPtr;

    if (prev == NULL)
        de_ctx->sc_sig_order_funcs = temp;
    else
        prev->next = temp;

    return;
}

/**
 * \brief Returns the flowbit type set for this signature.  If more than one
 *        flowbit has been set for the same rule, we return the flowbit type of
 *        the maximum priority/value, where priority/value is maximum for the
 *        ones that set the value and the lowest for ones that read the value.
 *        If no flowbit has been set for the rule, we return 0, which indicates
 *        the least value amongst flowbit types.
 *
 * \param sig Pointer to the Signature from which the flowbit value has to be
 *            returned.
 *
 * \retval flowbits The flowbits type for this signature if it is set; if it is
 *                  not set, return 0
 */
static inline int SCSigGetFlowbitsType(Signature *sig)
{
    SigMatch *sm = sig->match;
    DetectFlowbitsData *fb = NULL;
    int flowbits = 0;

    while (sm != NULL) {
        if (sm->type == DETECT_FLOWBITS) {
            fb = (DetectFlowbitsData *)sm->ctx;
            if (flowbits < fb->cmd)
                flowbits = fb->cmd;
        }

        sm = sm->next;
    }

    return flowbits;
}

/**
 * \brief Returns whether the flowvar set for this rule, sets the flowvar or
 *        reads the flowvar.  If the rule sets the flowvar the function returns
 *        DETECT_FLOWVAR_TYPE_SET(3), if it reads the flowvar the function
 *        returns DETECT_FLOWVAR_TYPE_READ(2), and if flowvar is not used in this
 *        rule the function returns DETECT_FLOWVAR_NOT_USED(1)
 *
 * \param sig Pointer to the Signature from which the flowvar type has to be
 *            returned.
 *
 * \retval type DETECT_FLOWVAR_TYPE_SET(3) if the rule sets the flowvar,
 *              DETECT_FLOWVAR_TYPE_READ(2) if it reads, and
 *              DETECT_FLOWVAR_NOT_USED(1) if flowvar is not used.
 */
static inline int SCSigGetFlowvarType(Signature *sig)
{
    SigMatch *sm = sig->match;
    DetectPcreData *pd = NULL;
    int type = DETECT_FLOWVAR_NOT_USED;

    while (sm != NULL) {
        pd = (DetectPcreData *)sm->ctx;
        if (sm->type == DETECT_PCRE && pd->flags & DETECT_PCRE_CAPTURE_FLOW) {
            type = DETECT_FLOWVAR_TYPE_SET;
            return type;
        }

        if (sm->type == DETECT_FLOWVAR)
            type = DETECT_FLOWVAR_TYPE_READ;

        sm = sm->next;
    }

    return type;
}

/**
 * \brief Returns whether the pktvar set for this rule, sets the flowvar or
 *        reads the pktvar.  If the rule sets the pktvar the function returns
 *        DETECT_PKTVAR_TYPE_SET(3), if it reads the pktvar the function
 *        returns DETECT_PKTVAR_TYPE_READ(2), and if pktvar is not used in this
 *        rule the function returns DETECT_PKTVAR_NOT_USED(1)
 *
 * \param sig Pointer to the Signature from which the pktvar type has to be
 *            returned.
 *
 * \retval type DETECT_PKTVAR_TYPE_SET(3) if the rule sets the flowvar,
 *              DETECT_PKTVAR_TYPE_READ(2) if it reads, and
 *              DETECT_PKTVAR_NOT_USED(1) if pktvar is not used.
 */
static inline int SCSigGetPktvarType(Signature *sig)
{
    SigMatch *sm = sig->match;
    DetectPcreData *pd = NULL;
    int type = DETECT_PKTVAR_NOT_USED;

    while (sm != NULL) {
        pd = (DetectPcreData *)sm->ctx;
        if (sm->type == DETECT_PCRE && pd->flags & DETECT_PCRE_CAPTURE_PKT) {
            type = DETECT_PKTVAR_TYPE_SET;
            return type;
        }

        if (sm->type == DETECT_PKTVAR)
            type = DETECT_PKTVAR_TYPE_READ;

        sm = sm->next;
    }

    return type;
}

/**
 * \brief Processes the flowbits data for this signature and caches it for
 *        future use.  This is needed to optimize the sig_ordering module.
 *
 * \param sw The sigwrapper/signature for which the flowbits data has to be
 *           cached
 */
static inline void SCSigProcessUserDataForFlowbits(SCSigSignatureWrapper *sw)
{
    *((int *)(sw->user[SC_RADIX_USER_DATA_FLOWBITS])) = SCSigGetFlowbitsType(sw->sig);

    return;
}

/**
 * \brief Processes the flowvar data for this signature and caches it for
 *        future use.  This is needed to optimize the sig_ordering module.
 *
 * \param sw The sigwrapper/signature for which the flowvar data has to be
 *           cached
 */
static inline void SCSigProcessUserDataForFlowvar(SCSigSignatureWrapper *sw)
{
    *((int *)(sw->user[SC_RADIX_USER_DATA_FLOWVAR])) = SCSigGetFlowvarType(sw->sig);

    return;
}

/**
 * \brief Processes the pktvar data for this signature and caches it for
 *        future use.  This is needed to optimize the sig_ordering module.
 *
 * \param sw The sigwrapper/signature for which the pktvar data has to be
 *           cached
 */
static inline void SCSigProcessUserDataForPktvar(SCSigSignatureWrapper *sw)
{
    *((int *)(sw->user[SC_RADIX_USER_DATA_PKTVAR])) = SCSigGetPktvarType(sw->sig);

    return;
}

/**
 * \brief Orders an incoming Signature based on its action
 *
 * \param de_ctx Pointer to the detection engine context from which the
 *               signatures have to be ordered.
 * \param sw     The new signature that has to be ordered based on its action
 */
static void SCSigOrderByAction(DetectEngineCtx *de_ctx,
                               SCSigSignatureWrapper *sw)
{
    SCSigSignatureWrapper *min = NULL;
    SCSigSignatureWrapper *max = NULL;
    SCSigSignatureWrapper *prev = NULL;

    if (de_ctx->sc_sig_sig_wrapper == NULL) {
        de_ctx->sc_sig_sig_wrapper = sw;
        sw->min = NULL;
        sw->max = NULL;
        return;
    }

    min = sw->min;
    max = sw->max;
    if (min == NULL)
        min = de_ctx->sc_sig_sig_wrapper;
    else
        min = min->next;

    while (min != max) {
        prev = min;
        /* the sorting logic */
        if (sw->sig->action <= min->sig->action) {
            min = min->next;
            continue;
        }

        if (min->prev == sw)
            break;

        if (sw->next != NULL)
            sw->next->prev = sw->prev;
        if (sw->prev != NULL)
            sw->prev->next = sw->next;
        if (de_ctx->sc_sig_sig_wrapper == sw)
            de_ctx->sc_sig_sig_wrapper = sw->next;

        sw->next = min;
        sw->prev = min->prev;

        if (min->prev != NULL)
            min->prev->next = sw;
        else
            de_ctx->sc_sig_sig_wrapper = sw;

        min->prev = sw;

        break;
    }

    if (min == max && prev != sw) {
        if (sw->next != NULL) {
            sw->next->prev = sw->prev;
            sw->prev->next = sw->next;
        }

        if (min == NULL) {
            prev->next = sw;
            sw->prev = prev;
            sw->next = NULL;
        } else {
            sw->prev = min->prev;
            sw->next = min;
            min->prev->next = sw;
            min->prev = sw;
        }
    }

    /* set the min signature for this keyword, for the next ordering function */
    min = sw;
    while (min != sw->min) {
        if (min->sig->action != sw->sig->action)
            break;

        min = min->prev;
    }
    sw->min = min;

    /* set the max signature for this keyword + 1, for the next ordering func */
    max = sw;
    while (max != sw->max) {
        if (max->sig->action != sw->sig->action)
            break;

        max = max->next;
    }
    sw->max = max;

    return;
}

/**
 * \brief Orders an incoming Signature based on its flowbits type
 *
 * \param de_ctx Pointer to the detection engine context from which the
 *               signatures have to be ordered.
 * \param sw     The new signature that has to be ordered based on its flowbits
 */
static void SCSigOrderByFlowbits(DetectEngineCtx *de_ctx,
                                 SCSigSignatureWrapper *sw)
{
    SCSigSignatureWrapper *min = NULL;
    SCSigSignatureWrapper *max = NULL;
    SCSigSignatureWrapper *prev = NULL;

    if (de_ctx->sc_sig_sig_wrapper == NULL) {
        de_ctx->sc_sig_sig_wrapper = sw;
        sw->min = NULL;
        sw->max = NULL;
        return;
    }

    min = sw->min;
    max = sw->max;
    if (min == NULL)
        min = de_ctx->sc_sig_sig_wrapper;
    else
        min = min->next;

    while (min != max) {
        prev = min;
        /* the sorting logic */
        if ( *((int *)(sw->user[SC_RADIX_USER_DATA_FLOWBITS])) <=
             *((int *)(min->user[SC_RADIX_USER_DATA_FLOWBITS])) ) {
            min = min->next;
            continue;
        }

        if (min->prev == sw)
            break;

        if (sw->next != NULL)
            sw->next->prev = sw->prev;
        if (sw->prev != NULL)
            sw->prev->next = sw->next;
        if (de_ctx->sc_sig_sig_wrapper == sw)
            de_ctx->sc_sig_sig_wrapper = sw->next;

        sw->next = min;
        sw->prev = min->prev;

        if (min->prev != NULL)
            min->prev->next = sw;
        else
            de_ctx->sc_sig_sig_wrapper = sw;

        min->prev = sw;

        break;
    }

    if (min == max && prev != sw) {
        if (sw->next != NULL) {
            sw->next->prev = sw->prev;
            sw->prev->next = sw->next;
        }

        if (min == NULL) {
            prev->next = sw;
            sw->prev = prev;
            sw->next = NULL;
        } else {
            sw->prev = min->prev;
            sw->next = min;
            min->prev->next = sw;
            min->prev = sw;
        }
    }

    /* set the min signature for this keyword, for the next ordering function */
    min = sw;
    while (min != sw->min) {
        if ( *((int *)(sw->user[SC_RADIX_USER_DATA_FLOWBITS])) !=
             *((int *)(min->user[SC_RADIX_USER_DATA_FLOWBITS])) )
            break;

        min = min->prev;
    }
    sw->min = min;

    /* set the max signature for this keyword + 1, for the next ordering func */
    max = sw;
    while (max != sw->max) {
        if ( *((int *)(sw->user[SC_RADIX_USER_DATA_FLOWBITS])) !=
             *((int *)(max->user[SC_RADIX_USER_DATA_FLOWBITS])) )
            break;

        max = max->next;
    }
    sw->max = max;

    return;

}

/**
 * \brief Orders an incoming Signature based on its flowvar type
 *
 * \param de_ctx Pointer to the detection engine context from which the
 *               signatures have to be ordered.
 * \param sw     The new signature that has to be ordered based on its flowvar
 */
static void SCSigOrderByFlowvar(DetectEngineCtx *de_ctx,
                                SCSigSignatureWrapper *sw)
{
    SCSigSignatureWrapper *min = NULL;
    SCSigSignatureWrapper *max = NULL;
    SCSigSignatureWrapper *prev = NULL;

    if (de_ctx->sc_sig_sig_wrapper == NULL) {
        de_ctx->sc_sig_sig_wrapper = sw;
        sw->min = NULL;
        sw->max = NULL;
        return;
    }

    min = sw->min;
    max = sw->max;
    if (min == NULL)
        min = de_ctx->sc_sig_sig_wrapper;
    else
        min = min->next;

    while (min != max) {
        prev = min;
        /* the sorting logic */
        if ( *((int *)(sw->user[SC_RADIX_USER_DATA_FLOWVAR])) <=
             *((int *)(min->user[SC_RADIX_USER_DATA_FLOWVAR])) ) {
            min = min->next;
            continue;
        }

        if (min->prev == sw)
            break;

        if (sw->next != NULL)
            sw->next->prev = sw->prev;
        if (sw->prev != NULL)
            sw->prev->next = sw->next;
        if (de_ctx->sc_sig_sig_wrapper == sw)
            de_ctx->sc_sig_sig_wrapper = sw->next;

        sw->next = min;
        sw->prev = min->prev;

        if (min->prev != NULL)
            min->prev->next = sw;
        else
            de_ctx->sc_sig_sig_wrapper = sw;

        min->prev = sw;

        break;
    }

    if (min == max && prev != sw) {
        if (sw->next != NULL) {
            sw->next->prev = sw->prev;
            sw->prev->next = sw->next;
        }

        if (min == NULL) {
            prev->next = sw;
            sw->prev = prev;
            sw->next = NULL;
        } else {
            sw->prev = min->prev;
            sw->next = min;
            min->prev->next = sw;
            min->prev = sw;
        }
    }

    /* set the min signature for this keyword, for the next ordering function */
    min = sw;
    while (min != sw->min) {
        if ( *((int *)(sw->user[SC_RADIX_USER_DATA_FLOWVAR])) !=
             *((int *)(min->user[SC_RADIX_USER_DATA_FLOWVAR])) )
            break;

        min = min->prev;
    }
    sw->min = min;

    /* set the max signature for this keyword + 1, for the next ordering func */
    max = sw;
    while (max != sw->max) {
        if ( *((int *)(sw->user[SC_RADIX_USER_DATA_FLOWVAR])) !=
             *((int *)(max->user[SC_RADIX_USER_DATA_FLOWVAR])) )
            break;

        max = max->next;
    }
    sw->max = max;

    return;
}

/**
 * \brief Orders an incoming Signature based on its pktvar type
 *
 * \param de_ctx Pointer to the detection engine context from which the
 *               signatures have to be ordered.
 * \param sw     The new signature that has to be ordered based on its pktvar
 */
static void SCSigOrderByPktvar(DetectEngineCtx *de_ctx,
                               SCSigSignatureWrapper *sw)
{
    SCSigSignatureWrapper *min = NULL;
    SCSigSignatureWrapper *max = NULL;
    SCSigSignatureWrapper *prev = NULL;

    if (de_ctx->sc_sig_sig_wrapper == NULL) {
        de_ctx->sc_sig_sig_wrapper = sw;
        sw->min = NULL;
        sw->max = NULL;
        return;
    }

    min = sw->min;
    max = sw->max;
    if (min == NULL)
        min = de_ctx->sc_sig_sig_wrapper;
    else
        min = min->next;
    while (min != max) {
        prev = min;
        /* the sorting logic */
        if ( *((int *)(sw->user[SC_RADIX_USER_DATA_PKTVAR])) <=
             *((int *)(min->user[SC_RADIX_USER_DATA_PKTVAR])) ) {
            min = min->next;
            continue;
        }

        if (min->prev == sw)
            break;

        if (sw->next != NULL)
            sw->next->prev = sw->prev;
        if (sw->prev != NULL)
            sw->prev->next = sw->next;
        if (de_ctx->sc_sig_sig_wrapper == sw)
            de_ctx->sc_sig_sig_wrapper = sw->next;

        sw->next = min;
        sw->prev = min->prev;

        if (min->prev != NULL)
            min->prev->next = sw;
        else
            de_ctx->sc_sig_sig_wrapper = sw;

        min->prev = sw;

        break;
    }

    if (min == max && prev != sw) {
        if (sw->next != NULL) {
            sw->next->prev = sw->prev;
            sw->prev->next = sw->next;
        }

        if (min == NULL) {
            prev->next = sw;
            sw->prev = prev;
            sw->next = NULL;
        } else {
            sw->prev = min->prev;
            sw->next = min;
            min->prev->next = sw;
            min->prev = sw;
        }
    }

    /* set the min signature for this keyword, for the next ordering function */
    min = sw;
    while (min != sw->min) {
        if ( *((int *)(sw->user[SC_RADIX_USER_DATA_PKTVAR])) !=
             *((int *)(min->user[SC_RADIX_USER_DATA_PKTVAR])) )
            break;

        min = min->prev;
    }
    sw->min = min;

    /* set the max signature for this keyword + 1, for the next ordering func */
    max = sw;
    while (max != sw->max) {
        if ( *((int *)(sw->user[SC_RADIX_USER_DATA_PKTVAR])) !=
             *((int *)(max->user[SC_RADIX_USER_DATA_PKTVAR])) )
            break;

        max = max->next;
    }
    sw->max = max;

    return;
}

/**
 * \brief Orders an incoming Signature based on its priority type
 *
 * \param de_ctx Pointer to the detection engine context from which the
 *               signatures have to be ordered.
 * \param sw     The new signature that has to be ordered based on its priority
 */
static void SCSigOrderByPriority(DetectEngineCtx *de_ctx,
                                 SCSigSignatureWrapper *sw)
{
    SCSigSignatureWrapper *min = NULL;
    SCSigSignatureWrapper *max = NULL;
    SCSigSignatureWrapper *prev = NULL;

    if (de_ctx->sc_sig_sig_wrapper == NULL) {
        de_ctx->sc_sig_sig_wrapper = sw;
        sw->min = NULL;
        sw->max = NULL;
        return;
    }

    min = sw->min;
    max = sw->max;
    if (min == NULL)
        min = de_ctx->sc_sig_sig_wrapper;
    else
        min = min->next;

    while (min != max) {
        prev = min;
        /* the sorting logic */
        if (sw->sig->prio >= min->sig->prio) {
            min = min->next;
            continue;
        }

        if (min->prev == sw)
            break;

        if (sw->next != NULL)
            sw->next->prev = sw->prev;
        if (sw->prev != NULL)
            sw->prev->next = sw->next;
        if (de_ctx->sc_sig_sig_wrapper == sw)
            de_ctx->sc_sig_sig_wrapper = sw->next;

        sw->next = min;
        sw->prev = min->prev;

        if (min->prev != NULL)
            min->prev->next = sw;
        else
            de_ctx->sc_sig_sig_wrapper = sw;

        min->prev = sw;

        break;
    }

    if (min == max && prev != sw) {
        if (sw->next != NULL) {
            sw->next->prev = sw->prev;
            sw->prev->next = sw->next;
        }

        if (min == NULL) {
            prev->next = sw;
            sw->prev = prev;
            sw->next = NULL;
        } else {
            sw->prev = min->prev;
            sw->next = min;
            min->prev->next = sw;
            min->prev = sw;
        }
    }

    /* set the min signature for this keyword, for the next ordering function */
    min = sw;
    while (min != sw->min) {
        if (min->sig->prio != sw->sig->prio)
            break;

        min = min->prev;
    }
    sw->min = min;

    /* set the max signature for this keyword + 1, for the next ordering func */
    max = sw;
    while (max != sw->max) {
        if (max->sig->prio != sw->sig->prio)
            break;

        max = max->next;
    }
    sw->max = max;

    return;
}

/**
 * \brief Creates a Wrapper around the Signature
 *
 * \param Pointer to the Signature to be wrapped
 *
 * \retval sw Pointer to the wrapper that holds the signature
 */
static inline SCSigSignatureWrapper *SCSigAllocSignatureWrapper(Signature *sig)
{
    SCSigSignatureWrapper *sw = NULL;
    int i = 0;

    if ( (sw = malloc(sizeof(SCSigSignatureWrapper))) == NULL) {
        printf("Error allocating memory\n");
        exit(EXIT_FAILURE);
    }
    memset(sw, 0, sizeof(SCSigSignatureWrapper));

    sw->sig = sig;

    if ( (sw->user = malloc(SC_RADIX_USER_DATA_MAX * sizeof(int *))) == NULL) {
        printf("Error allocating memory\n");
        exit(EXIT_FAILURE);
    }
    memset(sw->user, 0, SC_RADIX_USER_DATA_MAX * sizeof(int *));

    for (i = 0; i < SC_RADIX_USER_DATA_MAX; i++) {
        if ( (sw->user[i] = malloc(sizeof(int))) == NULL) {
            printf("Error allocating memory\n");
            exit(EXIT_FAILURE);
        }
        memset(sw->user[i], 0, sizeof(int));
    }

    /* Process data from the signature into a cache for further use by the
     * sig_ordering module */
    SCSigProcessUserDataForFlowbits(sw);
    SCSigProcessUserDataForFlowvar(sw);
    SCSigProcessUserDataForPktvar(sw);

    return sw;
}

/**
 * \brief Orders the signatures
 *
 * \param de_ctx Pointer to the Detection Engine Context that holds the
 *               signatures to be ordered
 */
void SCSigOrderSignatures(DetectEngineCtx *de_ctx)
{
    SCSigOrderFunc *funcs = NULL;
    Signature *sig = NULL;
    SCSigSignatureWrapper *sigw = NULL;

    int i = 0;
#ifndef UNITTESTS
    SCLogInfo("ordering signatures in memory");
#endif
    sig = de_ctx->sig_list;
    while (sig != NULL) {
        i++;
        sigw = SCSigAllocSignatureWrapper(sig);
        funcs = de_ctx->sc_sig_order_funcs;
        while (funcs != NULL) {
            funcs->FuncPtr(de_ctx, sigw);

            funcs = funcs->next;
        }
        sig = sig->next;
    }

#ifndef UNITTESTS
    printf("SCSigOrderSignatures: Total Signatures to be processed by the"
           "sigordering module: %d\n", i);
#endif

    /* Re-order it in the Detection Engine Context sig_list */
    de_ctx->sig_list = NULL;
    sigw = de_ctx->sc_sig_sig_wrapper;
    i = 0;
    while (sigw != NULL) {
        i++;
        if (de_ctx->sig_list == NULL) {
            sigw->sig->next = NULL;
            de_ctx->sig_list = sigw->sig;
            sig = de_ctx->sig_list;
            sigw = sigw->next;
            continue;
        }

        sigw->sig->next = NULL;
        sig->next = sigw->sig;
        sig = sig->next;
        sigw = sigw->next;
    }

#ifndef UNITTESTS
    SCLogInfo("total signatures reordered by the sigordering module: %d", i);
#endif
    return;
}

/**
 * \brief Lets you register the Signature ordering functions.  The order in
 *        which the functions are registered, show the priority.  The first
 *        function registered provides more priority than the function
 *        registered after it.  To add a new registration function, register
 *        it by listing it in the correct position in the below sequence,
 *        based on the priority you would want to offer to that keyword.
 *
 * \param de_ctx Pointer to the detection engine context from which the
 *               signatures have to be ordered.
 */
void SCSigRegisterSignatureOrderingFuncs(DetectEngineCtx *de_ctx)
{
    SCLogDebug("registering signature ordering functions");

    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByAction);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowbits);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPktvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPriority);

    return;
}

/**
 * \brief De-registers all the signature ordering functions registered
 *
 * \param de_ctx Pointer to the detection engine context from which the
 *               signatures were ordered.
 */
void SCSigSignatureOrderingModuleCleanup(DetectEngineCtx *de_ctx)
{
    SCSigOrderFunc *funcs = NULL;
    SCSigSignatureWrapper *sigw = NULL;
    void *temp = NULL;

    /* clean the memory alloted to the signature ordering funcs */
    funcs = de_ctx->sc_sig_order_funcs;
    while (funcs != NULL) {
        temp = funcs;
        funcs = funcs->next;
        free(temp);
    }
    de_ctx->sc_sig_order_funcs = NULL;

    /* clean the memory alloted to the signature wrappers */
    sigw = de_ctx->sc_sig_sig_wrapper;
    while (sigw != NULL) {
        temp = sigw;
        sigw = sigw->next;
        free(temp);
    }
    de_ctx->sc_sig_sig_wrapper = NULL;

    return;
}

/* -------------------------------------Unittests-----------------------------*/

DetectEngineCtx *DetectEngineCtxInit(void);
Signature *SigInit(DetectEngineCtx *, char *);
void SigFree(Signature *);
void DetectEngineCtxFree(DetectEngineCtx *);

#ifdef UNITTESTS

static int SCSigTestSignatureOrdering01(void)
{
    SCSigOrderFunc *temp = NULL;
    int i = 0;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByAction);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByAction);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByAction);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByAction);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByAction);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPriority);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowbits);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowbits);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPktvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowvar);

    temp = de_ctx->sc_sig_order_funcs;
    while (temp != NULL) {
        i++;
        temp = temp->next;
    }

    DetectEngineCtxFree(de_ctx);

    return (i == 5);
 end:
    return 0;
}

static int SCSigTestSignatureOrdering02(void)
{
    int result = 1;
    Signature *prevsig = NULL, *sig = NULL;
    SCSigSignatureWrapper *sw = NULL;
    int prev_code = 0;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:0; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig = sig;
    de_ctx->sig_list = sig;

    sig = SigInit(de_ctx, "drop tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:0; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "drop tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "pass tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:0; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; flowvar:http_host,\"www.oisf.net\"; rev:4; priority:1; )");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "reject tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:0; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:1;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "pass tcp any !21:902 -> any any (msg:\"Testing sigordering\"; pcre:\"/^User-Agent: (?P<flow_http_host>.*)\\r\\n/m\"; content:\"220\"; offset:10; depth:4; classtype:non-standard-protocol; sid:2003055; rev:4; priority:3;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "pass tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:3;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "pass tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:2;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;


    sig = SigInit(de_ctx, "reject tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; pcre:\"/^User-Agent: (?P<pkt_http_host>.*)\\r\\n/m\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:3; flowbits:set,TEST.one; flowbits:noalert;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "rejectsrc tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;priority:3;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "rejectdst tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "rejectboth tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "reject tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:12; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; pktvar:http_host,\"www.oisf.net\"; priority:2; flowbits:isnotset,TEST.two;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "reject tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:12; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:2; flowbits:set,TEST.two;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByAction);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowbits);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPktvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPriority);
    SCSigOrderSignatures(de_ctx);

    sw = de_ctx->sc_sig_sig_wrapper;
#ifdef DEBUG
    printf("%d - ", sw->sig->action);
    printf("%d - ", SCSigGetFlowbitsType(sw->sig));
    printf("%d - ", SCSigGetFlowvarType(sw->sig));
    printf("%d - ", SCSigGetPktvarType(sw->sig));
    printf("%d\n", sw->sig->prio);
#endif
    prev_code = sw->sig->action;
    sw = sw->next;
    while (sw != NULL) {
#ifdef DEBUG
        printf("%d - ", sw->sig->action);
        printf("%d - ", SCSigGetFlowbitsType(sw->sig));
        printf("%d - ", SCSigGetFlowvarType(sw->sig));
        printf("%d - ", SCSigGetPktvarType(sw->sig));
        printf("%d\n", sw->sig->prio);
#endif
        result &= (prev_code >= sw->sig->action);
        prev_code = sw->sig->action;
        sw = sw->next;
    }

    DetectEngineCtxFree(de_ctx);
end:
    return result;
}

static int SCSigTestSignatureOrdering03(void)
{
    int result = 1;
    Signature *prevsig = NULL, *sig = NULL;
    SCSigSignatureWrapper *sw = NULL;
    int prev_code = 0;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:0; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:3;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig = sig;
    de_ctx->sig_list = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:0; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:2;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; pcre:\"/^User-Agent: (?P<flow_http_host>.*)\\r\\n/m\"; classtype:non-standard-protocol; sid:2003055; flowbits:unset,TEST.one; rev:4; priority:2;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:0; depth:4; pcre:\"/^User-Agent: (?P<pkt_http_host>.*)\\r\\n/m\"; flowbits:isset,TEST.one; sid:2003055; rev:4; priority:1;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:0; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:2;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; flowbits:isnotset,TEST.one; pcre:\"/^User-Agent: (?P<flow_http_host>.*)\\r\\n/m\"; sid:2003055; rev:4;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; pcre:\"/220[- ]/\"; flowbits:unset,TEST.one; classtype:non-standard-protocol; sid:2003055; rev:4; priority:3;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; pcre:\"/220[- ]/\"; flowbits:toggle,TEST.one; classtype:non-standard-protocol; sid:2003055; rev:4; priority:1; pktvar:http_host,\"www.oisf.net\";)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;


    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; classtype:non-standard-protocol; sid:2003055; rev:4; flowbits:set,TEST.one; flowbits:noalert; pktvar:http_host,\"www.oisf.net\";)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:3;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:12; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; flowbits:isnotset,TEST.one;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:12; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; flowbits:set,TEST.one;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByAction);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowbits);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPktvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPriority);
    SCSigOrderSignatures(de_ctx);

    sw = de_ctx->sc_sig_sig_wrapper;
#ifdef DEBUG
    printf("%d\n", SCSigGetFlowbitsType(sw->sig));
#endif
    prev_code = SCSigGetFlowbitsType(sw->sig);
    sw = sw->next;
    while (sw != NULL) {
#ifdef DEBUG
        printf("%d\n", SCSigGetFlowbitsType(sw->sig));
#endif
        result &= (prev_code >= SCSigGetFlowbitsType(sw->sig));
        prev_code = SCSigGetFlowbitsType(sw->sig);
        sw = sw->next;
    }

    DetectEngineCtxFree(de_ctx);
end:
    return result;
}

static int SCSigTestSignatureOrdering04(void)
{
    int result = 1;
    Signature *prevsig = NULL, *sig = NULL;
    SCSigSignatureWrapper *sw = NULL;
    int prev_code = 0;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:0; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig = sig;
    de_ctx->sig_list = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; pcre:\"/^User-Agent: (?P<flow_http_host>.*)\\r\\n/m\"; content:\"220\"; offset:10; classtype:non-standard-protocol; sid:2003055; rev:4; priority:3;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; pcre:\"/^User-Agent: (?P<flow_http_host>.*)\\r\\n/m\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:3;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; pcre:\"/^User-Agent: (?P<flow_http_host>.*)\\r\\n/m\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:3; flowvar:http_host,\"www.oisf.net\";)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/^User-Agent: (?P<pkt_http_host>.*)\\r\\n/m\"; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;priority:3;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/^User-Agent: (?P<pkt_http_host>.*)\\r\\n/m\"; classtype:non-standard-protocol; pktvar:http_host,\"www.oisf.net\"; sid:2003055; rev:4; priority:1; )");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; flowvar:http_host,\"www.oisf.net\"; pktvar:http_host,\"www.oisf.net\"; priority:1;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:12; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:2; flowvar:http_host,\"www.oisf.net\";)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:12; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:2; flowvar:http_host,\"www.oisf.net\";)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByAction);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowbits);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPktvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPriority);
    SCSigOrderSignatures(de_ctx);

    sw = de_ctx->sc_sig_sig_wrapper;
#ifdef DEBUG
    printf("%d - ", SCSigGetFlowvarType(sw->sig));
#endif
    prev_code = SCSigGetFlowvarType(sw->sig);
    sw = sw->next;
    while (sw != NULL) {
#ifdef DEBUG
        printf("%d - ", SCSigGetFlowvarType(sw->sig));
#endif
        result &= (prev_code >= SCSigGetFlowvarType(sw->sig));
        prev_code = SCSigGetFlowvarType(sw->sig);
        sw = sw->next;
    }

    DetectEngineCtxFree(de_ctx);
end:
    return result;
}

static int SCSigTestSignatureOrdering05(void)
{
    int result = 1;
    Signature *prevsig = NULL, *sig = NULL;
    SCSigSignatureWrapper *sw = NULL;
    int prev_code = 0;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:0; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig = sig;
    de_ctx->sig_list = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; pcre:\"/^User-Agent: (?P<pkt_http_host>.*)\\r\\n/m\"; content:\"220\"; offset:10; classtype:non-standard-protocol; sid:2003055; rev:4; priority:3;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; pcre:\"/^User-Agent: (?P<pkt_http_host>.*)\\r\\n/m\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:3;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; pcre:\"/^User-Agent: (?P<pkt_http_host>.*)\\r\\n/m\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:3; pktvar:http_host,\"www.oisf.net\";)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;priority:3;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; pktvar:http_host,\"www.oisf.net\";)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:12; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:2; pktvar:http_host,\"www.oisf.net\";)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByAction);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowbits);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPktvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPriority);
    SCSigOrderSignatures(de_ctx);

    sw = de_ctx->sc_sig_sig_wrapper;
#ifdef DEBUG
    printf("%d - ", SCSigGetPktvarType(sw->sig));
#endif
    prev_code = SCSigGetPktvarType(sw->sig);
    sw = sw->next;
    while (sw != NULL) {
#ifdef DEBUG
        printf("%d - ", SCSigGetPktvarType(sw->sig));
#endif
        result &= (prev_code >= SCSigGetPktvarType(sw->sig));
        prev_code = SCSigGetPktvarType(sw->sig);
        sw = sw->next;
    }

    DetectEngineCtxFree(de_ctx);
end:
    return result;
}

static int SCSigTestSignatureOrdering06(void)
{
    int result = 1;
    Signature *prevsig = NULL, *sig = NULL;
    SCSigSignatureWrapper *sw = NULL;
    int prev_code = 0;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:0; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig = sig;
    de_ctx->sig_list = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; classtype:non-standard-protocol; sid:2003055; rev:4; priority:2;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; classtype:non-standard-protocol; sid:2003055; rev:4; priority:3;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:10; depth:4; classtype:non-standard-protocol; sid:2003055; rev:4; priority:2;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:2;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:1;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:11; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:2;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"Testing sigordering\"; content:\"220\"; offset:12; depth:4; pcre:\"/220[- ]/\"; classtype:non-standard-protocol; sid:2003055; rev:4; priority:2;)");
    if (sig == NULL) {
        goto end;
    }
    prevsig->next = sig;
    prevsig = sig;

    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByAction);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowbits);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByFlowvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPktvar);
    SCSigRegisterSignatureOrderingFunc(de_ctx, SCSigOrderByPriority);
    SCSigOrderSignatures(de_ctx);

    sw = de_ctx->sc_sig_sig_wrapper;
#ifdef DEBUG
    printf("%d - ", sw->sig->prio);
#endif
    prev_code = sw->sig->prio;
    sw = sw->next;
    while (sw != NULL) {
#ifdef DEBUG
        printf("%d - ", sw->sig->prio);
#endif
        result &= (prev_code <= sw->sig->prio);
        prev_code = sw->sig->prio;
        sw = sw->next;
    }

    DetectEngineCtxFree(de_ctx);
end:
    return result;
}

#endif

void SCSigRegisterSignatureOrderingTests(void)
{

#ifdef UNITTESTS

    UtRegisterTest("SCSigTestSignatureOrdering01", SCSigTestSignatureOrdering01, 1);
    UtRegisterTest("SCSigTestSignatureOrdering02", SCSigTestSignatureOrdering02, 1);
    UtRegisterTest("SCSigTestSignatureOrdering03", SCSigTestSignatureOrdering03, 1);
    UtRegisterTest("SCSigTestSignatureOrdering04", SCSigTestSignatureOrdering04, 1);
    UtRegisterTest("SCSigTestSignatureOrdering05", SCSigTestSignatureOrdering05, 1);
    UtRegisterTest("SCSigTestSignatureOrdering06", SCSigTestSignatureOrdering06, 1);

#endif

}
