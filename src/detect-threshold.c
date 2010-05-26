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
 * \author Breno Silva <breno.silva@gmail.com>
 *
 * Implements the threshold keyword
 */

#include "suricata-common.h"
#include "suricata.h"
#include "decode.h"

#include "detect.h"
#include "detect-parse.h"

#include "flow-var.h"
#include "decode-events.h"
#include "stream-tcp.h"

#include "detect-threshold.h"
#include "detect-parse.h"

#include "util-unittest.h"
#include "util-byte.h"
#include "util-debug.h"

#define PARSE_REGEX "^\\s*(track|type|count|seconds)\\s+(limit|both|threshold|by_dst|by_src|\\d+)\\s*,\\s*(track|type|count|seconds)\\s+(limit|both|threshold|by_dst|by_src|\\d+)\\s*,\\s*(track|type|count|seconds)\\s+(limit|both|threshold|by_dst|by_src|\\d+)\\s*,\\s*(track|type|count|seconds)\\s+(limit|both|threshold|by_dst|by_src|\\d+)\\s*"

static pcre *parse_regex;
static pcre_extra *parse_regex_study;

static int DetectThresholdMatch (ThreadVars *, DetectEngineThreadCtx *, Packet *, Signature *, SigMatch *);
static int DetectThresholdSetup (DetectEngineCtx *, Signature *, char *);
static void DetectThresholdFree(void *);

/**
 * \brief Registration function for threshold: keyword
 */

void DetectThresholdRegister (void) {
    sigmatch_table[DETECT_THRESHOLD].name = "threshold";
    sigmatch_table[DETECT_THRESHOLD].Match = DetectThresholdMatch;
    sigmatch_table[DETECT_THRESHOLD].Setup = DetectThresholdSetup;
    sigmatch_table[DETECT_THRESHOLD].Free  = DetectThresholdFree;
    sigmatch_table[DETECT_THRESHOLD].RegisterTests = ThresholdRegisterTests;
    /* this is compatible to ip-only signatures */
    sigmatch_table[DETECT_THRESHOLD].flags |= SIGMATCH_IPONLY_COMPAT;

    const char *eb;
    int opts = 0;
    int eo;

    parse_regex = pcre_compile(PARSE_REGEX, opts, &eb, &eo, NULL);
    if (parse_regex == NULL)
    {
        SCLogError(SC_ERR_PCRE_COMPILE, "pcre compile of \"%s\" failed at offset %" PRId32 ": %s", PARSE_REGEX, eo, eb);
        goto error;
    }

    parse_regex_study = pcre_study(parse_regex, 0, &eb);
    if (eb != NULL)
    {
        SCLogError(SC_ERR_PCRE_STUDY, "pcre study failed: %s", eb);
        goto error;
    }

error:
    return;

}

static int DetectThresholdMatch (ThreadVars *thv, DetectEngineThreadCtx *det_ctx, Packet *p, Signature *s, SigMatch *sm)
{
    return 1;
}

/**
 * \internal
 * \brief This function is used to parse threshold options passed via threshold: keyword
 *
 * \param rawstr Pointer to the user provided threshold options
 *
 * \retval de pointer to DetectThresholdData on success
 * \retval NULL on failure
 */
static DetectThresholdData *DetectThresholdParse (char *rawstr)
{
    DetectThresholdData *de = NULL;
#define MAX_SUBSTRINGS 30
    int ret = 0, res = 0;
    int ov[MAX_SUBSTRINGS];
    const char *str_ptr = NULL;
    char *args[9] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    char *copy_str = NULL, *threshold_opt = NULL;
    int second_found = 0, count_found = 0;
    int type_found = 0, track_found = 0;
    int second_pos = 0, count_pos = 0;
    uint16_t pos = 0;
    int i = 0;

    copy_str = SCStrdup(rawstr);

    for(pos = 0, threshold_opt = strtok(copy_str,",");  pos < strlen(copy_str) &&  threshold_opt != NULL;  pos++, threshold_opt = strtok(NULL,",")) {

        if(strstr(threshold_opt,"count"))
            count_found++;
        if(strstr(threshold_opt,"second"))
            second_found++;
        if(strstr(threshold_opt,"type"))
            type_found++;
        if(strstr(threshold_opt,"track"))
            track_found++;
    }

    if(copy_str)
        SCFree(copy_str);

    if(count_found != 1 || second_found != 1 || type_found != 1 || track_found != 1)
        goto error;

    ret = pcre_exec(parse_regex, parse_regex_study, rawstr, strlen(rawstr), 0, 0, ov, MAX_SUBSTRINGS);

    if (ret < 5) {
        SCLogError(SC_ERR_PCRE_MATCH, "pcre_exec parse error, ret %" PRId32 ", string %s", ret, rawstr);
        goto error;
    }

    de = SCMalloc(sizeof(DetectThresholdData));
    if (de == NULL)
        goto error;

    memset(de,0,sizeof(DetectThresholdData));

    for (i = 0; i < (ret - 1); i++) {

        res = pcre_get_substring((char *)rawstr, ov, MAX_SUBSTRINGS,i + 1, &str_ptr);

        if (res < 0) {
            SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed");
            goto error;
        }

        args[i] = (char *)str_ptr;

        if (strncasecmp(args[i],"limit",strlen("limit")) == 0)
            de->type = TYPE_LIMIT;
        if (strncasecmp(args[i],"both",strlen("both")) == 0)
            de->type = TYPE_BOTH;
        if (strncasecmp(args[i],"threshold",strlen("threshold")) == 0)
            de->type = TYPE_THRESHOLD;
        if (strncasecmp(args[i],"by_dst",strlen("by_dst")) == 0)
            de->track = TRACK_DST;
        if (strncasecmp(args[i],"by_src",strlen("by_src")) == 0)
            de->track = TRACK_SRC;
        if (strncasecmp(args[i],"count",strlen("seconds")) == 0)
            count_pos = i+1;
        if (strncasecmp(args[i],"seconds",strlen("seconds")) == 0)
            second_pos = i+1;
    }

    if (args[count_pos] == NULL || args[second_pos] == NULL) {
        goto error;
    }

    if (ByteExtractStringUint32(&de->count, 10, strlen(args[count_pos]),
                args[count_pos]) <= 0) {
        goto error;
    }

    if (ByteExtractStringUint32(&de->seconds, 10, strlen(args[second_pos]),
                args[second_pos]) <= 0) {
        goto error;
    }

    for (i = 0; i < (ret - 1); i++){
        if (args[i] != NULL) SCFree(args[i]);
    }
    return de;

error:
    for (i = 0; i < (ret - 1); i++){
        if (args[i] != NULL) SCFree(args[i]);
    }
    if (de) SCFree(de);
    return NULL;
}

/**
 * \internal
 * \brief this function is used to add the parsed threshold into the current signature
 *
 * \param de_ctx pointer to the Detection Engine Context
 * \param s pointer to the Current Signature
 * \param rawstr pointer to the user provided threshold options
 *
 * \retval 0 on Success
 * \retval -1 on Failure
 */
static int DetectThresholdSetup (DetectEngineCtx *de_ctx, Signature *s, char *rawstr)
{
    DetectThresholdData *de = NULL;
    SigMatch *sm = NULL;
    SigMatch *tmpm = NULL;

    /* checks if there is a previous instance of detection_filter */
    tmpm = SigMatchGetLastSM(s->match_tail, DETECT_DETECTION_FILTER);
    if (tmpm != NULL) {
        SCLogError(SC_ERR_INVALID_SIGNATURE, "\"detection_filter\" and \"threshold\" are not allowed in the same rule");
        SCReturnInt(-1);
    }

    de = DetectThresholdParse(rawstr);
    if (de == NULL)
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_THRESHOLD;
    sm->ctx = (void *)de;

    SigMatchAppendPacket(s, sm);

    return 0;

error:
    if (de) SCFree(de);
    if (sm) SCFree(sm);
    return -1;
}

/**
 * \internal
 * \brief this function will free memory associated with DetectThresholdData
 *
 * \param de pointer to DetectThresholdData
 */
static void DetectThresholdFree(void *de_ptr) {
    DetectThresholdData *de = (DetectThresholdData *)de_ptr;
    if (de) SCFree(de);
}

/*
 * ONLY TESTS BELOW THIS COMMENT
 */
#ifdef UNITTESTS

#include "detect-parse.h"
#include "detect-engine.h"
#include "detect-engine-mpm.h"
#include "detect-engine-threshold.h"
#include "util-time.h"
#include "util-hashlist.h"

/**
 * \test ThresholdTestParse01 is a test for a valid threshold options
 *
 *  \retval 1 on succces
 *  \retval 0 on failure
 */
static int ThresholdTestParse01 (void) {
    DetectThresholdData *de = NULL;
    de = DetectThresholdParse("type limit,track by_dst,count 10,seconds 60");
    if (de && (de->type == TYPE_LIMIT) && (de->track == TRACK_DST) && (de->count == 10) && (de->seconds == 60)) {
        DetectThresholdFree(de);
        return 1;
    }

    return 0;
}

/**
 * \test ThresholdTestParse02 is a test for a invalid threshold options
 *
 *  \retval 1 on succces
 *  \retval 0 on failure
 */
static int ThresholdTestParse02 (void) {
    DetectThresholdData *de = NULL;
    de = DetectThresholdParse("type any,track by_dst,count 10,seconds 60");
    if (de && (de->type == TYPE_LIMIT) && (de->track == TRACK_DST) && (de->count == 10) && (de->seconds == 60)) {
        DetectThresholdFree(de);
        return 1;
    }

    return 0;
}

/**
 * \test ThresholdTestParse03 is a test for a valid threshold options in any order
 *
 *  \retval 1 on succces
 *  \retval 0 on failure
 */
static int ThresholdTestParse03 (void) {
    DetectThresholdData *de = NULL;
    de = DetectThresholdParse("track by_dst, type limit, seconds 60, count 10");
    if (de && (de->type == TYPE_LIMIT) && (de->track == TRACK_DST) && (de->count == 10) && (de->seconds == 60)) {
        DetectThresholdFree(de);
        return 1;
    }

    return 0;
}


/**
 * \test ThresholdTestParse04 is a test for an invalid threshold options in any order
 *
 *  \retval 1 on succces
 *  \retval 0 on failure
 */
static int ThresholdTestParse04 (void) {
    DetectThresholdData *de = NULL;
    de = DetectThresholdParse("count 10, track by_dst, seconds 60, type both, count 10");
    if (de && (de->type == TYPE_BOTH) && (de->track == TRACK_DST) && (de->count == 10) && (de->seconds == 60)) {
        DetectThresholdFree(de);
        return 1;
    }

    return 0;
}

/**
 * \test ThresholdTestParse05 is a test for a valid threshold options in any order
 *
 *  \retval 1 on succces
 *  \retval 0 on failure
 */
static int ThresholdTestParse05 (void) {
    DetectThresholdData *de = NULL;
    de = DetectThresholdParse("count 10, track by_dst, seconds 60, type both");
    if (de && (de->type == TYPE_BOTH) && (de->track == TRACK_DST) && (de->count == 10) && (de->seconds == 60)) {
        DetectThresholdFree(de);
        return 1;
    }

    return 0;
}


/**
 * \test DetectThresholdTestSig1 is a test for checking the working of limit keyword
 *       by setting up the signature and later testing its working by matching
 *       the received packet against the sig.
 *
 *  \retval 1 on succces
 *  \retval 0 on failure
 */

static int DetectThresholdTestSig1(void) {

    Packet p;
    Signature *s = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx;
    int result = 0;
    int alerts = 0;
    IPV4Hdr ip4h;

    memset(&th_v, 0, sizeof(th_v));
    memset(&p, 0, sizeof(p));
    memset(&ip4h, 0, sizeof(ip4h));

    p.src.family = AF_INET;
    p.dst.family = AF_INET;
    p.proto = IPPROTO_TCP;
    p.ip4h = &ip4h;
    p.ip4h->ip_src.s_addr = 0x01010101;
    p.ip4h->ip_dst.s_addr = 0x02020202;
    p.sp = 1024;
    p.dp = 80;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    s = de_ctx->sig_list = SigInit(de_ctx,"alert tcp any any -> any 80 (msg:\"Threshold limit\"; threshold: type limit, track by_dst, count 5, seconds 60; sid:1;)");
    if (s == NULL) {
        goto end;
    }

    SigGroupBuild(de_ctx);

    if (s->flags & SIG_FLAG_IPONLY) {
        printf("signature is ip-only: ");
        goto end;
    }

    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts = PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);

    if(alerts == 5)
        result = 1;
    else
        printf("alerts %"PRIi32", expected 5: ", alerts);

    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);

    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);
    DetectEngineCtxFree(de_ctx);

end:
    return result;
}

/**
 * \test DetectThresholdTestSig2 is a test for checking the working of threshold keyword
 *       by setting up the signature and later testing its working by matching
 *       the received packet against the sig.
 *
 *  \retval 1 on succces
 *  \retval 0 on failure
 */

static int DetectThresholdTestSig2(void) {

    Packet p;
    Signature *s = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx;
    int result = 0;
    int alerts = 0;
    IPV4Hdr ip4h;

    memset(&th_v, 0, sizeof(th_v));
    memset(&p, 0, sizeof(p));
    memset(&ip4h, 0, sizeof(ip4h));

    p.src.family = AF_INET;
    p.dst.family = AF_INET;
    p.proto = IPPROTO_TCP;
    p.ip4h = &ip4h;
    p.ip4h->ip_src.s_addr = 0x01010101;
    p.ip4h->ip_dst.s_addr = 0x02020202;
    p.sp = 1024;
    p.dp = 80;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    s = de_ctx->sig_list = SigInit(de_ctx,"alert tcp any any -> any 80 (msg:\"Threshold\"; threshold: type threshold, track by_dst, count 5, seconds 60; sid:1;)");
    if (s == NULL) {
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts = PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);

    if (alerts == 2)
        result = 1;
    else
        goto cleanup;

cleanup:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);

    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);
    DetectEngineCtxFree(de_ctx);

end:
    return result;
}

/**
 * \test DetectThresholdTestSig3 is a test for checking the working of limit keyword
 *       by setting up the signature and later testing its working by matching
 *       the received packet against the sig.
 *
 *  \retval 1 on succces
 *  \retval 0 on failure
 */

static int DetectThresholdTestSig3(void) {

    Packet p;
    Signature *s = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx;
    int result = 0;
    int alerts = 0;
    IPV4Hdr ip4h;
    struct timeval ts;
    DetectThresholdData *td = NULL;
    DetectThresholdEntry *lookup_tsh = NULL;
    DetectThresholdEntry *ste = NULL;

    memset (&ts, 0, sizeof(struct timeval));
    TimeGet(&ts);

    memset(&th_v, 0, sizeof(th_v));
    memset(&p, 0, sizeof(p));
    memset(&ip4h, 0, sizeof(ip4h));

    p.src.family = AF_INET;
    p.dst.family = AF_INET;
    p.proto = IPPROTO_TCP;
    p.ip4h = &ip4h;
    p.ip4h->ip_src.s_addr = 0x01010101;
    p.ip4h->ip_dst.s_addr = 0x02020202;
    p.sp = 1024;
    p.dp = 80;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    s = de_ctx->sig_list = SigInit(de_ctx,"alert tcp any any -> any 80 (msg:\"Threshold limit\"; threshold: type limit, track by_dst, count 5, seconds 60; sid:10;)");
    if (s == NULL) {
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    td = SigGetThresholdType(s,&p);

    /* setup the Entry we use to search our hash with */
    ste = SCMalloc(sizeof(DetectThresholdEntry));
    if (ste == NULL)
        goto end;
    memset(ste, 0x00, sizeof(ste));

    if (PKT_IS_IPV4(&p))
        ste->ipv = 4;
    else if (PKT_IS_IPV6(&p))
        ste->ipv = 6;

    ste->sid = s->id;
    ste->gid = s->gid;

    if (td->track == TRACK_DST) {
        COPY_ADDRESS(&p.dst, &ste->addr);
    } else if (td->track == TRACK_SRC) {
        COPY_ADDRESS(&p.src, &ste->addr);
    }

    ste->track = td->track;

    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);

    lookup_tsh = (DetectThresholdEntry *)HashListTableLookup(de_ctx->ths_ctx.threshold_hash_table_dst, ste, sizeof(DetectThresholdEntry));
    if (lookup_tsh == NULL) {
        printf("lookup_tsh is NULL: ");
        goto cleanup;
    }

    TimeSetIncrementTime(200);

    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);

    if (lookup_tsh)
        alerts = lookup_tsh->current_count;

    if (alerts == 3)
        result = 1;
    else {
        printf("alerts %u != 3: ", alerts);
        goto cleanup;
    }

cleanup:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);

    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);
    DetectEngineCtxFree(de_ctx);
end:
    return result;
}

/**
 * \test DetectThresholdTestSig4 is a test for checking the working of both keyword
 *       by setting up the signature and later testing its working by matching
 *       the received packet against the sig.
 *
 *  \retval 1 on succces
 *  \retval 0 on failure
 */

static int DetectThresholdTestSig4(void) {

    Packet p;
    Signature *s = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx;
    int result = 0;
    int alerts = 0;
    IPV4Hdr ip4h;
    struct timeval ts;

    memset (&ts, 0, sizeof(struct timeval));
    TimeGet(&ts);

    memset(&th_v, 0, sizeof(th_v));
    memset(&p, 0, sizeof(p));
    memset(&ip4h, 0, sizeof(ip4h));

    p.src.family = AF_INET;
    p.dst.family = AF_INET;
    p.proto = IPPROTO_TCP;
    p.ip4h = &ip4h;
    p.ip4h->ip_src.s_addr = 0x01010101;
    p.ip4h->ip_dst.s_addr = 0x02020202;
    p.sp = 1024;
    p.dp = 80;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    s = de_ctx->sig_list = SigInit(de_ctx,"alert tcp any any -> any 80 (msg:\"Threshold both\"; threshold: type both, track by_dst, count 2, seconds 60; sid:10;)");
    if (s == NULL) {
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts = PacketAlertCheck(&p, 10);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 10);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 10);

    TimeSetIncrementTime(200);

    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 10);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 10);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 10);

    if (alerts == 2)
        result = 1;
    else
        goto cleanup;

cleanup:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);

    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);
    DetectEngineCtxFree(de_ctx);
end:
    return result;
}

/**
 * \test DetectThresholdTestSig5 is a test for checking the working of limit keyword
 *       by setting up the signature and later testing its working by matching
 *       the received packet against the sig.
 *
 *  \retval 1 on succces
 *  \retval 0 on failure
 */

static int DetectThresholdTestSig5(void) {

    Packet p;
    Signature *s = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx;
    int result = 0;
    int alerts = 0;
    IPV4Hdr ip4h;

    memset(&th_v, 0, sizeof(th_v));
    memset(&p, 0, sizeof(p));
    memset(&ip4h, 0, sizeof(ip4h));

    p.src.family = AF_INET;
    p.dst.family = AF_INET;
    p.proto = IPPROTO_TCP;
    p.ip4h = &ip4h;
    p.ip4h->ip_src.s_addr = 0x01010101;
    p.ip4h->ip_dst.s_addr = 0x02020202;
    p.sp = 1024;
    p.dp = 80;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    s = de_ctx->sig_list = SigInit(de_ctx,"alert tcp any any -> any 80 (msg:\"Threshold limit sid 1\"; threshold: type limit, track by_dst, count 5, seconds 60; sid:1;)");
    if (s == NULL) {
        goto end;
    }

    s = s->next = SigInit(de_ctx,"alert tcp any any -> any 80 (msg:\"Threshold limit sid 1000\"; threshold: type limit, track by_dst, count 5, seconds 60; sid:1000;)");
    if (s == NULL) {
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts = PacketAlertCheck(&p, 1);
    alerts += PacketAlertCheck(&p, 1000);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    alerts += PacketAlertCheck(&p, 1000);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    alerts += PacketAlertCheck(&p, 1000);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    alerts += PacketAlertCheck(&p, 1000);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    alerts += PacketAlertCheck(&p, 1000);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    alerts += PacketAlertCheck(&p, 1000);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    alerts += PacketAlertCheck(&p, 1000);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);
    alerts += PacketAlertCheck(&p, 1);
    alerts += PacketAlertCheck(&p, 1000);

    if(alerts == 10)
        result = 1;
    else
        goto cleanup;

cleanup:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);

    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);
    DetectEngineCtxFree(de_ctx);

end:
    return result;
}
#endif /* UNITTESTS */

void ThresholdRegisterTests(void) {
#ifdef UNITTESTS
    UtRegisterTest("ThresholdTestParse01", ThresholdTestParse01, 1);
    UtRegisterTest("ThresholdTestParse02", ThresholdTestParse02, 0);
    UtRegisterTest("ThresholdTestParse03", ThresholdTestParse03, 1);
    UtRegisterTest("ThresholdTestParse04", ThresholdTestParse04, 0);
    UtRegisterTest("ThresholdTestParse05", ThresholdTestParse05, 1);
    UtRegisterTest("DetectThresholdTestSig1", DetectThresholdTestSig1, 1);
    UtRegisterTest("DetectThresholdTestSig2", DetectThresholdTestSig2, 1);
    UtRegisterTest("DetectThresholdTestSig3", DetectThresholdTestSig3, 1);
    UtRegisterTest("DetectThresholdTestSig4", DetectThresholdTestSig4, 1);
    UtRegisterTest("DetectThresholdTestSig5", DetectThresholdTestSig5, 1);
#endif /* UNITTESTS */
}
