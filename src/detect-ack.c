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
 * \author Brian Rectanus <brectanu@gmail.com>
 *
 * Implements the "ack" keyword.
 */

#include "suricata-common.h"
#include "debug.h"
#include "decode.h"
#include "detect.h"

#include "detect-parse.h"
#include "detect-engine.h"
#include "detect-engine-mpm.h"

#include "detect-ack.h"

#include "util-byte.h"
#include "util-unittest.h"
#include "util-debug.h"

/* prototypes */
static int DetectAckSetup(DetectEngineCtx *, Signature *, char *);
static int DetectAckMatch(ThreadVars *, DetectEngineThreadCtx *,
                          Packet *, Signature *, SigMatch *);
static void DetectAckRegisterTests(void);
static void DetectAckFree(void *);

void DetectAckRegister(void) {
    sigmatch_table[DETECT_ACK].name = "ack";
    sigmatch_table[DETECT_ACK].Match = DetectAckMatch;
    sigmatch_table[DETECT_ACK].Setup = DetectAckSetup;
    sigmatch_table[DETECT_ACK].Free = DetectAckFree;
    sigmatch_table[DETECT_ACK].RegisterTests = DetectAckRegisterTests;
}

/**
 * \internal
 * \brief This function is used to match packets with a given Ack number
 *
 * \param t pointer to thread vars
 * \param det_ctx pointer to the pattern matcher thread
 * \param p pointer to the current packet
 * \param m pointer to the sigmatch that we will cast into DetectAckData
 *
 * \retval 0 no match
 * \retval 1 match
 */
static int DetectAckMatch(ThreadVars *t, DetectEngineThreadCtx *det_ctx,
                          Packet *p, Signature *s, SigMatch *m)
{
    DetectAckData *data = (DetectAckData *)m->ctx;

    /* This is only needed on TCP packets */
    if (IPPROTO_TCP != p->proto) {
        return 0;
    }

    return (data->ack == TCP_GET_ACK(p)) ? 1 : 0;
}

/**
 * \internal
 * \brief this function is used to add the ack option into the signature
 *
 * \param de_ctx pointer to the Detection Engine Context
 * \param s pointer to the Current Signature
 * \param m pointer to the Current SigMatch
 * \param optstr pointer to the user provided options
 *
 * \retval 0 on Success
 * \retval -1 on Failure
 */
static int DetectAckSetup(DetectEngineCtx *de_ctx, Signature *s, char *optstr)
{
    DetectAckData *data;
    SigMatch *sm = NULL;

    data = SCMalloc(sizeof(DetectAckData));
    if (data == NULL)
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL) {
        goto error;
    }

    sm->type = DETECT_ACK;

    if (-1 == ByteExtractStringUint32(&data->ack, 10, 0, optstr)) {
        goto error;
    }
    sm->ctx = data;

    SigMatchAppendPacket(s, sm);

    return 0;

error:
    if (data) SCFree(data);
    return -1;

}

/**
 * \internal
 * \brief this function will free memory associated with ack option
 *
 * \param data pointer to ack configuration data
 */
static void DetectAckFree(void *ptr)
{
    DetectAckData *data = (DetectAckData *)ptr;
    SCFree(data);
}


#ifdef UNITTESTS
/**
 * \internal
 * \brief This test tests sameip success and failure.
 */
static int DetectAckSigTest01Real(int mpm_type)
{
    uint8_t *buf = (uint8_t *)"";
    uint16_t buflen = strlen((char *)buf);
    Packet p[3];
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx;
    int result = 0;
    uint8_t tcp_hdr0[] = {
        0x00, 0x50, 0x8e, 0x16, 0x0d, 0x59, 0xcd, 0x3c,
        0xcf, 0x0d, 0x21, 0x80, 0xa0, 0x12, 0x16, 0xa0,
        0xfa, 0x03, 0x00, 0x00, 0x02, 0x04, 0x05, 0xb4,
        0x04, 0x02, 0x08, 0x0a, 0x6e, 0x18, 0x78, 0x73,
        0x01, 0x71, 0x74, 0xde, 0x01, 0x03, 0x03, 0x02
    };
    uint8_t tcp_hdr1[] = {
        0x00, 0x50, 0x8e, 0x16, 0x0d, 0x59, 0xcd, 0x3c,
        0xcf, 0x0d, 0x21, 0x80, 0xa0, 0x12, 0x16, 0xa0,
        0xfa, 0x03, 0x00, 0x00, 0x02, 0x04, 0x05, 0xb4,
        0x04, 0x02, 0x08, 0x0a, 0x6e, 0x18, 0x78, 0x73,
        0x01, 0x71, 0x74, 0xde, 0x01, 0x03, 0x03, 0x02
    };

    memset(&th_v, 0, sizeof(th_v));

    /* TCP w/ack=42 */
    memset(&p[0], 0, sizeof(p[0]));
    p[0].src.family = AF_INET;
    p[0].dst.family = AF_INET;
    p[0].payload = buf;
    p[0].payload_len = buflen;
    p[0].proto = IPPROTO_TCP;
    p[0].tcph = (TCPHdr *)tcp_hdr0;
    p[0].tcph->th_ack = htonl(42);

    /* TCP w/ack=100 */
    memset(&p[1], 0, sizeof(p[1]));
    p[1].src.family = AF_INET;
    p[1].dst.family = AF_INET;
    p[1].payload = buf;
    p[1].payload_len = buflen;
    p[1].proto = IPPROTO_TCP;
    p[1].tcph = (TCPHdr *)tcp_hdr1;
    p[1].tcph->th_ack = htonl(100);

    /* ICMP */
    memset(&p[2], 0, sizeof(p[2]));
    p[2].src.family = AF_INET;
    p[2].dst.family = AF_INET;
    p[2].payload = buf;
    p[2].payload_len = buflen;
    p[2].proto = IPPROTO_ICMP;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->mpm_matcher = mpm_type;
    de_ctx->flags |= DE_QUIET;

    /* These three are crammed in here as there is no Parse */
    if (SigInit(de_ctx,
                "alert tcp any any -> any any "
                "(msg:\"Testing ack\";ack:foo;sid:1;)") != NULL)
    {
        printf("invalid ack accepted: ");
        goto cleanup_engine;
    }
    if (SigInit(de_ctx,
                "alert tcp any any -> any any "
                "(msg:\"Testing ack\";ack:9999999999;sid:1;)") != NULL)
    {
        printf("overflowing ack accepted: ");
        goto cleanup_engine;
    }
    if (SigInit(de_ctx,
                "alert tcp any any -> any any "
                "(msg:\"Testing ack\";ack:-100;sid:1;)") != NULL)
    {
        printf("negative ack accepted: ");
        goto cleanup_engine;
    }

    de_ctx->sig_list = SigInit(de_ctx,
                               "alert tcp any any -> any any "
                               "(msg:\"Testing ack\";ack:41;sid:1;)");
    if (de_ctx->sig_list == NULL) {
        goto cleanup_engine;
    }

    de_ctx->sig_list->next = SigInit(de_ctx,
                                     "alert tcp any any -> any any "
                                     "(msg:\"Testing ack\";ack:42;sid:2;)");
    if (de_ctx->sig_list->next == NULL) {
        goto cleanup_engine;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p[0]);
    if (PacketAlertCheck(&p[0], 1) != 0) {
        printf("sid 1 alerted, but should not have: ");
        goto cleanup;
    }
    if (PacketAlertCheck(&p[0], 2) == 0) {
        printf("sid 2 did not alert, but should have: ");
        goto cleanup;
    }

    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p[1]);
    if (PacketAlertCheck(&p[1], 1) != 0) {
        printf("sid 1 alerted, but should not have: ");
        goto cleanup;
    }
    if (PacketAlertCheck(&p[1], 2) != 0) {
        printf("sid 2 alerted, but should not have: ");
        goto cleanup;
    }

    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p[1]);
    if (PacketAlertCheck(&p[2], 1) != 0) {
        printf("sid 1 alerted, but should not have: ");
        goto cleanup;
    }
    if (PacketAlertCheck(&p[2], 2) != 0) {
        printf("sid 2 alerted, but should not have: ");
        goto cleanup;
    }

    result = 1;

cleanup:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);

    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);

cleanup_engine:
    DetectEngineCtxFree(de_ctx);

end:
    return result;
}

/**
 * \test DetectAckSigTest01B2g tests sameip under B2g MPM
 */
static int DetectAckSigTest01B2g(void)
{
    return DetectAckSigTest01Real(MPM_B2G);
}

/**
 * \test DetectAckSigTest01B2g tests sameip under B3g MPM
 */
static int DetectAckSigTest01B3g(void)
{
    return DetectAckSigTest01Real(MPM_B3G);
}

/**
 * \test DetectAckSigTest01B2g tests sameip under WuManber MPM
 */
static int DetectAckSigTest01Wm(void)
{
    return DetectAckSigTest01Real(MPM_WUMANBER);
}

#endif /* UNITTESTS */

/**
 * \internal
 * \brief This function registers unit tests for DetectAck
 */
static void DetectAckRegisterTests(void)
{
#ifdef UNITTESTS
    UtRegisterTest("DetectAckSigTest01B2g", DetectAckSigTest01B2g, 1);
    UtRegisterTest("DetectAckSigTest01B3g", DetectAckSigTest01B3g, 1);
    UtRegisterTest("DetectAckSigTest01Wm", DetectAckSigTest01Wm, 1);
#endif /* UNITTESTS */
}

