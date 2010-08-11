/* Copyright (C) 2007-2010 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Gurvinder Singh <gurvindersighdahiya@gmail.com>
 *
 * Implements the urilen keyword
 */

#include "suricata-common.h"
#include "app-layer-protos.h"
#include "app-layer-htp.h"
#include "util-unittest.h"
#include "util-unittest-helper.h"

#include "detect.h"
#include "detect-parse.h"
#include "detect-engine-state.h"

#include "detect-urilen.h"
#include "util-debug.h"
#include "util-byte.h"
#include "flow-util.h"
#include "stream-tcp.h"

/**
 * \brief Regex for parsing our urilen
 */
#define PARSE_REGEX  "^\\s*(<|>)?\\s*([0-9]{1,5})\\s*(?:(<>)\\s*([0-9]{1,5}))?\\s*$"

static pcre *parse_regex;
static pcre_extra *parse_regex_study;

/*prototypes*/
int DetectUrilenMatch (ThreadVars *t, DetectEngineThreadCtx *det_ctx, Flow *f,
                       uint8_t flags, void *state, Signature *s, SigMatch *m);
static int DetectUrilenSetup (DetectEngineCtx *, Signature *, char *);
void DetectUrilenFree (void *);
void DetectUrilenRegisterTests (void);

/**
 * \brief Registration function for urilen: keyword
 */

void DetectUrilenRegister(void)
{
    sigmatch_table[DETECT_AL_URILEN].name = "urilen";
    sigmatch_table[DETECT_AL_URILEN].Match = NULL;
    sigmatch_table[DETECT_AL_URILEN].alproto = ALPROTO_HTTP;
    sigmatch_table[DETECT_AL_URILEN].AppLayerMatch = NULL /**< We handle this at detect-engine-uri.c now */;
    sigmatch_table[DETECT_AL_URILEN].Setup = DetectUrilenSetup;
    sigmatch_table[DETECT_AL_URILEN].Free = DetectUrilenFree;
    sigmatch_table[DETECT_AL_URILEN].RegisterTests = DetectUrilenRegisterTests;
    sigmatch_table[DETECT_AL_HTTP_METHOD].flags |= SIGMATCH_PAYLOAD;

    const char *eb;
    int eo;
    int opts = 0;

    parse_regex = pcre_compile(PARSE_REGEX, opts, &eb, &eo, NULL);
    if (parse_regex == NULL) {
        SCLogDebug("pcre compile of \"%s\" failed at offset %" PRId32 ": %s",
                    PARSE_REGEX, eo, eb);
        goto error;
    }

    parse_regex_study = pcre_study(parse_regex, 0, &eb);
    if (eb != NULL) {
        SCLogDebug("pcre study failed: %s", eb);
        goto error;
    }
    return;

error:
    if (parse_regex != NULL) SCFree(parse_regex);
    if (parse_regex_study != NULL) SCFree(parse_regex_study);
    return;
}

/**
 * \brief   This function is used to match urilen rule option with the HTTP
 *          uricontent.
 *
 * \param t pointer to thread vars
 * \param det_ctx pointer to the pattern matcher thread
 * \param p pointer to the current packet
 * \param m pointer to the sigmatch that we will cast into DetectUrilenData
 *
 * \retval 0 no match
 * \retval 1 match
 */
int DetectUrilenMatch (ThreadVars *t, DetectEngineThreadCtx *det_ctx, Flow *f,
                       uint8_t flags, void *state, Signature *s, SigMatch *m)
{
    SCEnter();
    int ret = 0;
    size_t idx = 0;
    DetectUrilenData *urilend = (DetectUrilenData *) m->ctx;

    HtpState *htp_state = (HtpState *)state;
    if (htp_state == NULL) {
        SCLogDebug("no HTP state, no need to match further");
        SCReturnInt(ret);
    }

    SCMutexLock(&f->m);
    htp_tx_t *tx = NULL;

    for (idx = 0;//htp_state->new_in_tx_index;
         idx < list_size(htp_state->connp->conn->transactions); idx++)
    {
        tx = list_get(htp_state->connp->conn->transactions, idx);
        if (tx == NULL || tx->request_uri_normalized == NULL)
            goto end;

        switch (urilend->mode) {
            case DETECT_URILEN_EQ:
                if (bstr_len(tx->request_uri_normalized) == urilend->urilen1)
                    ret = 1;
                break;
            case DETECT_URILEN_LT:
                if (bstr_len(tx->request_uri_normalized) < urilend->urilen1)
                    ret = 1;
                break;
            case DETECT_URILEN_GT:
                if (bstr_len(tx->request_uri_normalized) > urilend->urilen1)
                    ret = 1;
                break;
            case DETECT_URILEN_RA:
                if (bstr_len(tx->request_uri_normalized) > urilend->urilen1 &&
                        bstr_len(tx->request_uri_normalized) < urilend->urilen2)
                    ret = 1;
                break;
        }
    }
end:
    SCMutexUnlock(&f->m);
    SCReturnInt(ret);
}

/**
 * \brief This function is used to parse urilen options passed via urilen: keyword
 *
 * \param urilenstr Pointer to the user provided urilen options
 *
 * \retval urilend pointer to DetectUrilenData on success
 * \retval NULL on failure
 */

DetectUrilenData *DetectUrilenParse (char *urilenstr)
{

    DetectUrilenData *urilend = NULL;
    char *arg1 = NULL;
    char *arg2 = NULL;
    char *arg3 = NULL;
    char *arg4 = NULL;
#define MAX_SUBSTRINGS 30
    int ret = 0, res = 0;
    int ov[MAX_SUBSTRINGS];

    ret = pcre_exec(parse_regex, parse_regex_study, urilenstr, strlen(urilenstr),
                    0, 0, ov, MAX_SUBSTRINGS);
    if (ret < 3 || ret > 5) {
        SCLogError(SC_ERR_PCRE_PARSE, "parse error, ret %" PRId32 "", ret);
        goto error;
    }
    const char *str_ptr;

    res = pcre_get_substring((char *)urilenstr, ov, MAX_SUBSTRINGS, 1, &str_ptr);
    if (res < 0) {
        SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed");
        goto error;
    }
    arg1 = (char *) str_ptr;
    SCLogDebug("Arg1 \"%s\"", arg1);

    res = pcre_get_substring((char *)urilenstr, ov, MAX_SUBSTRINGS, 2, &str_ptr);
    if (res < 0) {
        SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed");
        goto error;
    }
    arg2 = (char *) str_ptr;
    SCLogDebug("Arg2 \"%s\"", arg2);

    res = pcre_get_substring((char *)urilenstr, ov, MAX_SUBSTRINGS, 3, &str_ptr);
    if (res < 0) {
        SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed");
        goto error;
    }
    arg3 = (char *) str_ptr;
    SCLogDebug("Arg3 \"%s\"", arg3);

    res = pcre_get_substring((char *)urilenstr, ov, MAX_SUBSTRINGS, 4, &str_ptr);
    if (res < 0) {
        SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed");
        goto error;
    }
    arg4 = (char *) str_ptr;
    SCLogDebug("Arg4 \"%s\"", arg4);

    urilend = SCMalloc(sizeof (DetectUrilenData));
    if (urilend == NULL)
        goto error;
    urilend->urilen1 = 0;
    urilend->urilen2 = 0;

    if (arg1[0] == '<') urilend->mode = DETECT_URILEN_LT;
    else if (arg1[0] == '>') urilend->mode = DETECT_URILEN_GT;
    else urilend->mode = DETECT_URILEN_EQ;

    if (strcmp("<>", arg3) == 0) {
        if (strlen(arg1) != 0) {
            SCLogError(SC_ERR_INVALID_ARGUMENT,"Range specified but mode also set");
            goto error;
        }
        urilend->mode = DETECT_URILEN_RA;
    }

    /** set the first urilen value */
    if(ByteExtractStringUint16(&urilend->urilen1,10,strlen(arg2),arg2) <= 0){
        SCLogError(SC_ERR_INVALID_ARGUMENT,"Invalid size :\"%s\"",arg2);
        goto error;
    }

    /** set the second urilen value if specified */
    if (strlen(arg4) > 0) {
        if (urilend->mode != DETECT_URILEN_RA) {
            SCLogError(SC_ERR_INVALID_ARGUMENT,"Multiple urilen values specified"
                                           " but mode is not range");
            goto error;
        }

        if(ByteExtractStringUint16(&urilend->urilen2,10,strlen(arg4),arg4) <= 0)
        {
            SCLogError(SC_ERR_INVALID_ARGUMENT,"Invalid size :\"%s\"",arg4);
            goto error;
        }

        if (urilend->urilen2 <= urilend->urilen1){
            SCLogError(SC_ERR_INVALID_ARGUMENT,"urilen2:%"PRIu16" <= urilen:"
                        "%"PRIu16"",urilend->urilen2,urilend->urilen1);
            goto error;
        }
    }

    SCFree(arg1);
    SCFree(arg2);
    SCFree(arg3);
    SCFree(arg4);
    return urilend;

error:
    if (urilend) SCFree(urilend);
    if (arg1) SCFree(arg1);
    if (arg2) SCFree(arg2);
    if (arg3) SCFree(arg3);
    if (arg4) SCFree(arg4);
    return NULL;
}

/**
 * \brief this function is used to parse urilen data into the current signature
 *
 * \param de_ctx pointer to the Detection Engine Context
 * \param s pointer to the Current Signature
 * \param urilenstr pointer to the user provided urilen options
 *
 * \retval 0 on Success
 * \retval -1 on Failure
 */
static int DetectUrilenSetup (DetectEngineCtx *de_ctx, Signature *s, char *urilenstr)
{
    SCEnter();
    DetectUrilenData *urilend = NULL;
    SigMatch *sm = NULL;

    urilend = DetectUrilenParse(urilenstr);
    if (urilend == NULL)
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_AL_URILEN;
    sm->ctx = (void *)urilend;

    SigMatchAppendUricontent(s,sm);

    /* Flagged the signature as to inspect the app layer data */
    s->flags |= SIG_FLAG_APPLAYER;

    SCReturnInt(0);

error:
    if (urilend != NULL) DetectUrilenFree(urilend);
    if (sm != NULL) SCFree(sm);
    SCReturnInt(-1);
}

/**
 * \brief this function will free memory associated with DetectUrilenData
 *
 * \param ptr pointer to DetectUrilenData
 */
void DetectUrilenFree(void *ptr)
{
    DetectUrilenData *urilend = (DetectUrilenData *)ptr;
    SCFree(urilend);
}

#ifdef UNITTESTS

#include "stream.h"
#include "stream-tcp-private.h"
#include "stream-tcp-reassemble.h"
#include "detect-parse.h"
#include "detect-engine.h"
#include "detect-engine-mpm.h"
#include "app-layer-parser.h"

/** \test   Test the Urilen keyword setup */
static int DetectUrilenParseTest01(void)
{
    int ret = 0;
    DetectUrilenData *urilend = NULL;

    urilend = DetectUrilenParse("10");
    if (urilend != NULL) {
        if (urilend->urilen1 == 10 && urilend->mode == DETECT_URILEN_EQ)
            ret = 1;

        DetectUrilenFree(urilend);
    }
    return ret;
}

/** \test   Test the Urilen keyword setup */
static int DetectUrilenParseTest02(void)
{
    int ret = 0;
    DetectUrilenData *urilend = NULL;

    urilend = DetectUrilenParse(" < 10  ");
    if (urilend != NULL) {
        if (urilend->urilen1 == 10 && urilend->mode == DETECT_URILEN_LT)
            ret = 1;

        DetectUrilenFree(urilend);
    }
    return ret;
}

/** \test   Test the Urilen keyword setup */
static int DetectUrilenParseTest03(void)
{
    int ret = 0;
    DetectUrilenData *urilend = NULL;

    urilend = DetectUrilenParse(" > 10 ");
    if (urilend != NULL) {
        if (urilend->urilen1 == 10 && urilend->mode == DETECT_URILEN_GT)
            ret = 1;

        DetectUrilenFree(urilend);
    }
    return ret;
}

/** \test   Test the Urilen keyword setup */
static int DetectUrilenParseTest04(void)
{
    int ret = 0;
    DetectUrilenData *urilend = NULL;

    urilend = DetectUrilenParse(" 5 <> 10 ");
    if (urilend != NULL) {
        if (urilend->urilen1 == 5 && urilend->urilen2 == 10 &&
                urilend->mode == DETECT_URILEN_RA)
            ret = 1;

        DetectUrilenFree(urilend);
    }
    return ret;
}

/**
 * \brief this function is used to initialize the detection engine context and
 *        setup the signature with passed values.
 *
 */

static int DetectUrilenInitTest(DetectEngineCtx **de_ctx, Signature **sig,
                                DetectUrilenData **urilend, char *str)
{
    char fullstr[1024];
    int result = 0;

    *de_ctx = NULL;
    *sig = NULL;

    if (snprintf(fullstr, 1024, "alert ip any any -> any any (msg:\"Urilen "
                                "test\"; urilen:%s; sid:1;)", str) >= 1024) {
        goto end;
    }

    *de_ctx = DetectEngineCtxInit();
    if (*de_ctx == NULL) {
        goto end;
    }

    (*de_ctx)->flags |= DE_QUIET;

    (*de_ctx)->sig_list = SigInit(*de_ctx, fullstr);
    if ((*de_ctx)->sig_list == NULL) {
        goto end;
    }

    *sig = (*de_ctx)->sig_list;

    *urilend = DetectUrilenParse(str);

    result = 1;

end:
    return result;
}

/**
 * \test DetectUrilenSetpTest01 is a test for setting up an valid urilen values
 *       with valid "<>" operator and include spaces arround the given values.
 *       In the test the values are setup with initializing the detection engine
 *       context and setting up the signature itself.
 */

static int DetectUrilenSetpTest01(void) {

    DetectUrilenData *urilend = NULL;
    uint8_t res = 0;
    Signature *sig = NULL;
    DetectEngineCtx *de_ctx = NULL;

    res = DetectUrilenInitTest(&de_ctx, &sig, &urilend, "1 <> 2 ");
    if (res == 0) {
        goto end;
    }

    if(urilend == NULL)
        goto cleanup;

    if (urilend != NULL) {
        if (urilend->urilen1 == 1 && urilend->urilen2 == 2 &&
                urilend->mode == DETECT_URILEN_RA)
            res = 1;
    }

cleanup:
    if (urilend) SCFree(urilend);
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
end:
    return res;
}

/** \test Check a signature with gievn urilen */
static int DetectUrilenSigTest01(void)
{
    int result = 0;
    Flow f;
    uint8_t httpbuf1[] = "POST /suricata HTTP/1.0\r\n"
                         "Host: foo.bar.tld\r\n"
                         "\r\n";
    uint32_t httplen1 = sizeof(httpbuf1) - 1; /* minus the \0 */
    TcpSession ssn;
    Packet *p = NULL;
    Signature *s = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx;

    memset(&th_v, 0, sizeof(th_v));
    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));

    p = UTHBuildPacket(NULL, 0, IPPROTO_TCP);

    FLOW_INITIALIZE(&f);
    f.protoctx = (void *)&ssn;
    f.src.family = AF_INET;
    f.dst.family = AF_INET;

    p->flow = &f;
    p->flowflags |= FLOW_PKT_TOSERVER;
    p->flowflags |= FLOW_PKT_ESTABLISHED;
    f.alproto = ALPROTO_HTTP;

    StreamTcpInitConfig(TRUE);
    FlowL7DataPtrInit(&f);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    s = de_ctx->sig_list = SigInit(de_ctx,
                                   "alert tcp any any -> any any "
                                   "(msg:\"Testing urilen\"; "
                                   "urilen: <5; sid:1;)");
    if (s == NULL) {
        goto end;
    }

    s = s->next = SigInit(de_ctx,
                          "alert tcp any any -> any any "
                          "(msg:\"Testing http_method\"; "
                           "urilen: >5; sid:2;)");
    if (s == NULL) {
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    int r = AppLayerParse(&f, ALPROTO_HTTP, STREAM_TOSERVER, httpbuf1, httplen1);
    if (r != 0) {
        SCLogDebug("toserver chunk 1 returned %" PRId32 ", expected 0: ", r);
        goto end;
    }

    HtpState *htp_state = f.aldata[AlpGetStateIdx(ALPROTO_HTTP)];
    if (htp_state == NULL) {
        SCLogDebug("no http state: ");
        goto end;
    }

    SigMatchSignatures(&th_v, de_ctx, det_ctx, p);

    if ((PacketAlertCheck(p, 1))) {
        printf("sid 1 alerted, but should not have: \n");
        goto end;
    }
    if (!PacketAlertCheck(p, 2)) {
        printf("sid 2 did not alerted, but should have: \n");
        goto end;
    }

    result = 1;

end:
    if (de_ctx != NULL) SigGroupCleanup(de_ctx);
    if (de_ctx != NULL) SigCleanSignatures(de_ctx);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);

    FlowL7DataPtrFree(&f);
    StreamTcpFreeConfig(TRUE);
    FLOW_DESTROY(&f);
    UTHFreePackets(&p, 1);
    return result;
}

#endif /* UNITTESTS */

/**
 * \brief this function registers unit tests for DetectUrilen
 */
void DetectUrilenRegisterTests(void)
{
#ifdef UNITTESTS
    UtRegisterTest("DetectUrilenParseTest01", DetectUrilenParseTest01, 1);
    UtRegisterTest("DetectUrilenParseTest02", DetectUrilenParseTest02, 1);
    UtRegisterTest("DetectUrilenParseTest03", DetectUrilenParseTest03, 1);
    UtRegisterTest("DetectUrilenParseTest04", DetectUrilenParseTest04, 1);
    UtRegisterTest("DetectUrilenSetpTest01", DetectUrilenSetpTest01, 1);
    UtRegisterTest("DetectUrilenSigTest01", DetectUrilenSigTest01, 1);
#endif /* UNITTESTS */
}
