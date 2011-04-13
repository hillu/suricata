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
 * \author Victor Julien <victor@inliniac.net>
 *
 * Implements the nocase keyword
 */

#include "suricata-common.h"
#include "decode.h"

#include "detect.h"
#include "detect-parse.h"

#include "flow-var.h"

#include "detect-content.h"
#include "detect-uricontent.h"
#include "detect-pcre.h"
#include "detect-http-client-body.h"
#include "detect-http-cookie.h"
#include "detect-http-header.h"
#include "detect-http-method.h"
#include "detect-http-uri.h"

#include "util-debug.h"

static int DetectNocaseSetup (DetectEngineCtx *, Signature *, char *);

void DetectNocaseRegister (void) {
    sigmatch_table[DETECT_NOCASE].name = "nocase";
    sigmatch_table[DETECT_NOCASE].Match = NULL;
    sigmatch_table[DETECT_NOCASE].Setup = DetectNocaseSetup;
    sigmatch_table[DETECT_NOCASE].Free  = NULL;
    sigmatch_table[DETECT_NOCASE].RegisterTests = NULL;

    sigmatch_table[DETECT_NOCASE].flags |= SIGMATCH_NOOPT;
    sigmatch_table[DETECT_NOCASE].flags |= SIGMATCH_PAYLOAD;
}

/**
 *  \internal
 *  \brief get the last pattern sigmatch that supports nocase:
 *         content, uricontent, http_client_body, http_cookie
 *
 *  \param s signature
 *
 *  \retval sm sigmatch of either content or uricontent that is the last
 *             or NULL if none was found
 */
static SigMatch *SigMatchGetLastNocasePattern(Signature *s) {
    SCEnter();

    BUG_ON(s == NULL);

    SigMatch *co_sm = DetectContentGetLastPattern(s->pmatch_tail);
    SigMatch *ur_sm = SigMatchGetLastSM(s->umatch_tail, DETECT_URICONTENT);
    /* http client body SigMatch */
    SigMatch *hcbd_sm = SigMatchGetLastSM(s->amatch_tail, DETECT_AL_HTTP_CLIENT_BODY);
    /* http cookie SigMatch */
    SigMatch *hcd_sm = SigMatchGetLastSM(s->amatch_tail, DETECT_AL_HTTP_COOKIE);
    /* http header SigMatch */
    SigMatch *hhd_sm = SigMatchGetLastSM(s->amatch_tail, DETECT_AL_HTTP_HEADER);
    /* http method SigMatch */
    SigMatch *hmd_sm = SigMatchGetLastSM(s->amatch_tail, DETECT_AL_HTTP_METHOD);

    SigMatch *temp_sm = NULL;

    SigMatch **sm_list = NULL;
    uint8_t sm_list_count = 0;

    if (co_sm != NULL) {
        sm_list_count++;
        if ( (sm_list = SCRealloc(sm_list, sizeof(SigMatch *) * sm_list_count)) == NULL) {
            SCLogError(SC_ERR_FATAL, "Fatal error encountered in SigMatchGetLastNocasePattern. Exiting...");
            exit(EXIT_FAILURE);
        }
        sm_list[sm_list_count - 1] = co_sm;
    }
    if (ur_sm != NULL) {
        sm_list_count++;
        if ( (sm_list = SCRealloc(sm_list, sizeof(SigMatch *) * sm_list_count)) == NULL) {
            SCLogError(SC_ERR_FATAL, "Fatal error encountered in SigMatchGetLastNocasePattern. Exiting...");
            exit(EXIT_FAILURE);
        }
        sm_list[sm_list_count - 1] = ur_sm;
    }
    if (hcbd_sm != NULL) {
        sm_list_count++;
        if ( (sm_list = SCRealloc(sm_list, sizeof(SigMatch *) * sm_list_count)) == NULL) {
            SCLogError(SC_ERR_FATAL, "Fatal error encountered in SigMatchGetLastNocasePattern. Exiting...");
            exit(EXIT_FAILURE);
        }
        sm_list[sm_list_count - 1] = hcbd_sm;
    }
    if (hcd_sm != NULL) {
        sm_list_count++;
        if ( (sm_list = SCRealloc(sm_list, sizeof(SigMatch *) * sm_list_count)) == NULL) {
            SCLogError(SC_ERR_FATAL, "Fatal error encountered in SigMatchGetLastNocasePattern. Exiting...");
            exit(EXIT_FAILURE);
        }
        sm_list[sm_list_count - 1] = hcd_sm;
    }
    if (hhd_sm != NULL) {
        sm_list_count++;
        if ( (sm_list = SCRealloc(sm_list, sizeof(SigMatch *) * sm_list_count)) == NULL) {
            SCLogError(SC_ERR_FATAL, "Fatal error encountered in SigMatchGetLastNocasePattern. Exiting...");
            exit(EXIT_FAILURE);
        }
        sm_list[sm_list_count - 1] = hhd_sm;
    }

    if (hmd_sm != NULL) {
        sm_list_count++;
        if ( (sm_list = SCRealloc(sm_list, sizeof(SigMatch *) * sm_list_count)) == NULL) {
            SCLogError(SC_ERR_FATAL, "Fatal error encountered in SigMatchGetLastNocasePattern. Exiting...");
            exit(EXIT_FAILURE);
        }
        sm_list[sm_list_count - 1] = hmd_sm;
    }

    if (sm_list_count == 0)
        SCReturnPtr(NULL, "SigMatch");

    /* find the highest idx sm, so we apply to the last sm that we support */
    int i = 0, j = 0;
    int swapped = 1;
    while (swapped) {
        swapped = 0;
        for (j = i; j < sm_list_count - 1; j++) {
            if (sm_list[j]->idx < sm_list[j + 1]->idx) {
                temp_sm = sm_list[j];
                sm_list[j] = sm_list[j + 1];
                sm_list[j + 1] = temp_sm;
                swapped = 1;
                i++;
            }
        }
    }

    temp_sm = sm_list[0];
    SCFree(sm_list);

    SCReturnPtr(temp_sm, "SigMatch");
}

/**
 *  \internal
 *  \brief Apply the nocase keyword to the last pattern match, either content or uricontent
 *  \param det_ctx detection engine ctx
 *  \param s signature
 *  \param nullstr should be null
 *  \retval 0 ok
 *  \retval -1 failure
 */
static int DetectNocaseSetup (DetectEngineCtx *de_ctx, Signature *s, char *nullstr)
{
    SCEnter();

    if (nullstr != NULL) {
        SCLogError(SC_ERR_INVALID_VALUE, "nocase has value");
        SCReturnInt(-1);
    }

    /* Search for the first previous SigMatch that supports nocase */
    SigMatch *pm = SigMatchGetLastNocasePattern(s);
    if (pm == NULL) {
        SCLogError(SC_ERR_NOCASE_MISSING_PATTERN, "\"nocase\" needs a preceeding"
                " content, uricontent, http_client_body, http_header, http_method, http_uri, http_cookie option");
        SCReturnInt(-1);
    }

    DetectUricontentData *ud = NULL;
    DetectContentData *cd = NULL;
    DetectHttpClientBodyData *dhcb = NULL;
    DetectHttpCookieData *dhcd = NULL;
    DetectHttpHeaderData *dhhd = NULL;
    DetectHttpMethodData *dhmd = NULL;

    switch (pm->type) {
        case DETECT_URICONTENT:
            ud = (DetectUricontentData *)pm->ctx;
            if (ud == NULL) {
                SCLogError(SC_ERR_INVALID_ARGUMENT, "invalid argument");
                SCReturnInt(-1);
            }
            ud->flags |= DETECT_URICONTENT_NOCASE;
            /* Recreate the context with nocase chars */
            BoyerMooreCtxToNocase(ud->bm_ctx, ud->uricontent, ud->uricontent_len);
            break;

        case DETECT_CONTENT:
            cd = (DetectContentData *)pm->ctx;
            if (cd == NULL) {
                SCLogError(SC_ERR_INVALID_ARGUMENT, "invalid argument");
                SCReturnInt(-1);
            }
            cd->flags |= DETECT_CONTENT_NOCASE;
            /* Recreate the context with nocase chars */
            BoyerMooreCtxToNocase(cd->bm_ctx, cd->content, cd->content_len);
            break;
        case DETECT_AL_HTTP_CLIENT_BODY:
            dhcb =(DetectHttpClientBodyData *) pm->ctx;
            dhcb->flags |= DETECT_AL_HTTP_CLIENT_BODY_NOCASE;
            /* Recreate the context with nocase chars */
            BoyerMooreCtxToNocase(dhcb->bm_ctx, dhcb->content, dhcb->content_len);
            break;
        case DETECT_AL_HTTP_HEADER:
            dhhd =(DetectHttpHeaderData *) pm->ctx;
            dhhd->flags |= DETECT_AL_HTTP_HEADER_NOCASE;
            break;
        case DETECT_AL_HTTP_METHOD:
            dhmd =(DetectHttpMethodData *) pm->ctx;
            dhmd->flags |= DETECT_AL_HTTP_METHOD_NOCASE;
            break;
        case DETECT_AL_HTTP_COOKIE:
            dhcd = (DetectHttpCookieData *) pm->ctx;
            dhcd->flags |= DETECT_AL_HTTP_COOKIE_NOCASE;
            break;
            /* should never happen */
        default:
            SCLogError(SC_ERR_NOCASE_MISSING_PATTERN, "\"nocase\" needs a"
                    " preceeding content, uricontent, http_client_body or http_cookie option");
            SCReturnInt(-1);
            break;
    }

    SCReturnInt(0);
}

