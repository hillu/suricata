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
 * FLOW part of the detection engine.
 */

#include "suricata-common.h"
#include "debug.h"
#include "decode.h"

#include "detect.h"
#include "detect-parse.h"

#include "flow.h"
#include "flow-var.h"

#include "detect-flow.h"

#include "util-unittest.h"
#include "util-debug.h"

/**
 * \brief Regex for parsing our flow options
 */
#define PARSE_REGEX  "^\\s*([A-z_]+)\\s*(?:,\\s*([A-z_]+))?\\s*(?:,\\s*([A-z_]+))?\\s*$"

static pcre *parse_regex;
static pcre_extra *parse_regex_study;

int DetectFlowMatch (ThreadVars *, DetectEngineThreadCtx *, Packet *, Signature *, SigMatch *);
static int DetectFlowSetup (DetectEngineCtx *, Signature *, char *);
void DetectFlowRegisterTests(void);
void DetectFlowFree(void *);

/**
 * \brief Registration function for flow: keyword
 * \todo add support for no_stream and stream_only
 */
void DetectFlowRegister (void) {
    sigmatch_table[DETECT_FLOW].name = "flow";
    sigmatch_table[DETECT_FLOW].Match = DetectFlowMatch;
    sigmatch_table[DETECT_FLOW].Setup = DetectFlowSetup;
    sigmatch_table[DETECT_FLOW].Free  = DetectFlowFree;
    sigmatch_table[DETECT_FLOW].RegisterTests = DetectFlowRegisterTests;

    const char *eb;
    int eo;
    int opts = 0;

    parse_regex = pcre_compile(PARSE_REGEX, opts, &eb, &eo, NULL);
    if(parse_regex == NULL)
    {
        SCLogError(SC_ERR_PCRE_COMPILE, "pcre compile of \"%s\" failed at offset %" PRId32 ": %s", PARSE_REGEX, eo, eb);
        goto error;
    }

    parse_regex_study = pcre_study(parse_regex, 0, &eb);
    if(eb != NULL)
    {
        SCLogError(SC_ERR_PCRE_STUDY, "pcre study failed: %s", eb);
        goto error;
    }
    return;

error:
    /* XXX */
    return;
}

/*
 * returns 0: no match
 *         1: match
 *        -1: error
 */

/**
 * \brief This function is used to match flow flags set on a packet with those passed via flow:
 * \todo We need to add support for no_stream and stream_only flag checking
 *
 * \param t pointer to thread vars
 * \param det_ctx pointer to the pattern matcher thread
 * \param p pointer to the current packet
 * \param m pointer to the sigmatch that we will cast into DetectFlowData
 *
 * \retval 0 no match
 * \retval 1 match
 */
int DetectFlowMatch (ThreadVars *t, DetectEngineThreadCtx *det_ctx, Packet *p, Signature *s, SigMatch *m)
{
    uint8_t cnt = 0;
    DetectFlowData *fd = (DetectFlowData *)m->ctx;

    if (fd->flags & FLOW_PKT_TOSERVER && p->flowflags & FLOW_PKT_TOSERVER) {
        cnt++;
    } else if (fd->flags & FLOW_PKT_TOCLIENT && p->flowflags & FLOW_PKT_TOCLIENT) {
        cnt++;
    }

    if (fd->flags & FLOW_PKT_ESTABLISHED && p->flowflags & FLOW_PKT_ESTABLISHED) {
        cnt++;
    } else if (fd->flags & FLOW_PKT_STATELESS) {
        cnt++;
    }

    int ret = (fd->match_cnt == cnt) ? 1 : 0;
    //printf("DetectFlowMatch: returning %" PRId32 " cnt %" PRId32 " fd->match_cnt %" PRId32 " fd->flags 0x%02X p->flowflags 0x%02X \n", ret, cnt,
              //fd->match_cnt, fd->flags, p->flowflags);
    return ret;
}

/**
 * \brief This function is used to parse flow options passed via flow: keyword
 *
 * \param flowstr Pointer to the user provided flow options
 *
 * \retval fd pointer to DetectFlowData on success
 * \retval NULL on failure
 */
DetectFlowData *DetectFlowParse (char *flowstr)
{
    DetectFlowData *fd = NULL;
    char *args[3] = {NULL,NULL,NULL};
#define MAX_SUBSTRINGS 30
    int ret = 0, res = 0;
    int ov[MAX_SUBSTRINGS];

    ret = pcre_exec(parse_regex, parse_regex_study, flowstr, strlen(flowstr), 0, 0, ov, MAX_SUBSTRINGS);
    if (ret < 1 || ret > 4) {
        SCLogError(SC_ERR_PCRE_MATCH, "parse error, ret %" PRId32 ", string %s", ret, flowstr);
        goto error;
    }
    if (ret > 1) {
        const char *str_ptr;
        res = pcre_get_substring((char *)flowstr, ov, MAX_SUBSTRINGS, 1, &str_ptr);
        if (res < 0) {
            SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed");
            goto error;
        }
        args[0] = (char *)str_ptr;

        if (ret > 2) {
            res = pcre_get_substring((char *)flowstr, ov, MAX_SUBSTRINGS, 2, &str_ptr);
            if (res < 0) {
                SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed");
                goto error;
            }
            args[1] = (char *)str_ptr;
        }
        if (ret > 3) {
            res = pcre_get_substring((char *)flowstr, ov, MAX_SUBSTRINGS, 3, &str_ptr);
            if (res < 0) {
                SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed");
                goto error;
            }
            args[2] = (char *)str_ptr;
        }
    }

    fd = SCMalloc(sizeof(DetectFlowData));
    if (fd == NULL) {
        SCLogError(SC_ERR_MEM_ALLOC, "malloc failed");
        goto error;
    }
    fd->flags = 0;
    fd->match_cnt = 0;

    int i;
    for (i = 0; i < (ret -1); i++) {
        if (args[i]) {
            /* inspect our options and set the flags */
            if (strcasecmp(args[i], "established") == 0) {
                if (fd->flags & FLOW_PKT_ESTABLISHED) {
                    SCLogError(SC_ERR_FLAGS_MODIFIER, "FLOW_PKT_ESTABLISHED flag is already set");
                    goto error;
                } else if (fd->flags & FLOW_PKT_STATELESS) {
                    SCLogError(SC_ERR_FLAGS_MODIFIER, "FLOW_PKT_STATELESS already set");
                    goto error;
                }
                fd->flags |= FLOW_PKT_ESTABLISHED;
            } else if (strcasecmp(args[i], "stateless") == 0) {
                if (fd->flags & FLOW_PKT_STATELESS) {
                    SCLogError(SC_ERR_FLAGS_MODIFIER, "FLOW_PKT_STATELESS flag is already set");
                    goto error;
                } else if (fd->flags & FLOW_PKT_ESTABLISHED) {
                    SCLogError(SC_ERR_FLAGS_MODIFIER, "cannot set FLOW_PKT_STATELESS, FLOW_PKT_ESTABLISHED already set");
                    goto error;
                }
                fd->flags |= FLOW_PKT_STATELESS;
            } else if (strcasecmp(args[i], "to_client") == 0 || strcasecmp(args[i], "from_server") == 0) {
                if (fd->flags & FLOW_PKT_TOCLIENT) {
                    SCLogError(SC_ERR_FLAGS_MODIFIER, "cannot set FLOW_PKT_TOCLIENT flag is already set");
                    goto error;
                } else if (fd->flags & FLOW_PKT_TOSERVER) {
                    SCLogError(SC_ERR_FLAGS_MODIFIER, "cannot set to_client, FLOW_PKT_TOSERVER already set");
                    goto error;
                }
                fd->flags |= FLOW_PKT_TOCLIENT;
            } else if (strcasecmp(args[i], "to_server") == 0 || strcasecmp(args[i], "from_client") == 0){
                if (fd->flags & FLOW_PKT_TOSERVER) {
                    SCLogError(SC_ERR_FLAGS_MODIFIER, "cannot set FLOW_PKT_TOSERVER flag is already set");
                    goto error;
                } else if (fd->flags & FLOW_PKT_TOCLIENT) {
                    SCLogError(SC_ERR_FLAGS_MODIFIER, "cannot set to_server, FLOW_PKT_TO_CLIENT flag already set");
                    goto error;
                }
                fd->flags |= FLOW_PKT_TOSERVER;
            } else if (strcasecmp(args[i], "stream_only") == 0) {
                if (fd->flags & FLOW_PKT_STREAMONLY) {
                    SCLogError(SC_ERR_FLAGS_MODIFIER, "cannot set stream_only flag is already set");
                    goto error;
                } else if (fd->flags & FLOW_PKT_NOSTREAM) {
                    SCLogError(SC_ERR_FLAGS_MODIFIER, "cannot set stream_only flag, FLOW_PKT_NOSTREAM already set");
                    goto error;
                }
                fd->flags |= FLOW_PKT_STREAMONLY;
            } else if (strcasecmp(args[i], "no_stream") == 0) {
                if (fd->flags & FLOW_PKT_NOSTREAM) {
                    SCLogError(SC_ERR_FLAGS_MODIFIER, "cannot set no_stream flag is already set");
                    goto error;
                } else if (fd->flags & FLOW_PKT_STREAMONLY) {
                    SCLogError(SC_ERR_FLAGS_MODIFIER, "cannot set no_stream flag, FLOW_PKT_STREAMONLY already set");
                    goto error;
                }
                fd->flags |= FLOW_PKT_NOSTREAM;
            } else {
                SCLogError(SC_ERR_INVALID_VALUE, "invalid flow option \"%s\"", args[i]);
                goto error;
            }

            fd->match_cnt++;
            //printf("args[%" PRId32 "]: %s match_cnt: %" PRId32 " flags: 0x%02X\n", i, args[i], fd->match_cnt, fd->flags);
        }
    }
    for (i = 0; i < (ret -1); i++){
        if (args[i] != NULL) SCFree(args[i]);
    }
    return fd;

error:
    for (i = 0; i < (ret -1); i++){
        if (args[i] != NULL) SCFree(args[i]);
    }
    if (fd != NULL) DetectFlowFree(fd);
    return NULL;

}

/**
 * \brief this function is used to add the parsed flowdata into the current signature
 *
 * \param de_ctx pointer to the Detection Engine Context
 * \param s pointer to the Current Signature
 * \param flowstr pointer to the user provided flow options
 *
 * \retval 0 on Success
 * \retval -1 on Failure
 */
int DetectFlowSetup (DetectEngineCtx *de_ctx, Signature *s, char *flowstr)
{
    DetectFlowData *fd = NULL;
    SigMatch *sm = NULL;

    //printf("DetectFlowSetup: \'%s\'\n", flowstr);

    fd = DetectFlowParse(flowstr);
    if (fd == NULL) goto error;

    /* Okay so far so good, lets get this into a SigMatch
     * and put it in the Signature. */
    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_FLOW;
    sm->ctx = (void *)fd;

    SigMatchAppendPacket(s, sm);

    s->flags |= SIG_FLAG_FLOW;
    return 0;

error:
    if (fd != NULL) DetectFlowFree(fd);
    if (sm != NULL) SCFree(sm);
    return -1;

}

/**
 * \brief this function will free memory associated with DetectFlowData
 *
 * \param fd pointer to DetectFlowData
 */
void DetectFlowFree(void *ptr) {
    DetectFlowData *fd = (DetectFlowData *)ptr;
    SCFree(fd);
}

#ifdef UNITTESTS

/**
 * \test DetectFlowTestParse01 is a test to make sure that we return "something"
 *  when given valid flow opt
 */
int DetectFlowTestParse01 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("established");
    if (fd != NULL) {
        DetectFlowFree(fd);
        result = 1;
    }

    return result;
}

/**
 * \test DetectFlowTestParse02 is a test for setting the established flow opt
 */
int DetectFlowTestParse02 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("established");
    if (fd != NULL) {
        if (fd->flags == FLOW_PKT_ESTABLISHED && fd->match_cnt == 1) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_ESTABLISHED, 1, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse03 is a test for setting the stateless flow opt
 */
int DetectFlowTestParse03 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("stateless");
    if (fd != NULL) {
        if (fd->flags == FLOW_PKT_STATELESS && fd->match_cnt == 1) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_STATELESS, 1, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse04 is a test for setting the to_client flow opt
 */
int DetectFlowTestParse04 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("to_client");
    if (fd != NULL) {
        if (fd->flags == FLOW_PKT_TOCLIENT && fd->match_cnt == 1) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_TOCLIENT, 1, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse05 is a test for setting the to_server flow opt
 */
int DetectFlowTestParse05 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("to_server");
    if (fd != NULL) {
        if (fd->flags == FLOW_PKT_TOSERVER && fd->match_cnt == 1) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_TOSERVER, 1, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse06 is a test for setting the from_server flow opt
 */
int DetectFlowTestParse06 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("from_server");
    if (fd != NULL) {
        if (fd->flags == FLOW_PKT_TOCLIENT && fd->match_cnt == 1) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_TOCLIENT, 1, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse07 is a test for setting the from_client flow opt
 */
int DetectFlowTestParse07 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("from_client");
    if (fd != NULL) {
        if (fd->flags == FLOW_PKT_TOSERVER && fd->match_cnt == 1) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_TOSERVER, 1, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse08 is a test for setting the established,to_client flow opts
 */
int DetectFlowTestParse08 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("established,to_client");
    if (fd != NULL) {
        if (fd->flags & FLOW_PKT_ESTABLISHED && fd->flags & FLOW_PKT_TOCLIENT && fd->match_cnt == 2) {
            result = 1;
        } else {
            printf("expected: 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_ESTABLISHED + FLOW_PKT_TOCLIENT, 2, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse09 is a test for setting the to_client,stateless flow opts (order of state,dir reversed)
 */
int DetectFlowTestParse09 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("to_client,stateless");
    if (fd != NULL) {
        if (fd->flags & FLOW_PKT_STATELESS && fd->flags & FLOW_PKT_TOCLIENT && fd->match_cnt == 2) {
            result = 1;
        } else {
            printf("expected: 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_STATELESS + FLOW_PKT_TOCLIENT, 2, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse10 is a test for setting the from_server,stateless flow opts (order of state,dir reversed)
 */
int DetectFlowTestParse10 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("from_server,stateless");
    if (fd != NULL) {
        if (fd->flags & FLOW_PKT_STATELESS  && fd->flags & FLOW_PKT_TOCLIENT && fd->match_cnt == 2){
            result = 1;
        } else {
            printf("expected: 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_STATELESS + FLOW_PKT_TOCLIENT, 2, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse11 is a test for setting the from_server,stateless flow opts with spaces all around
 */
int DetectFlowTestParse11 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse(" from_server , stateless ");
    if (fd != NULL) {
        if (fd->flags & FLOW_PKT_STATELESS  && fd->flags & FLOW_PKT_TOCLIENT && fd->match_cnt == 2){
            result = 1;
        } else {
            printf("expected: 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_STATELESS + FLOW_PKT_TOCLIENT, 2, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParseNocase01 is a test to make sure that we return "something"
 *  when given valid flow opt
 */
int DetectFlowTestParseNocase01 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("ESTABLISHED");
    if (fd != NULL) {
        DetectFlowFree(fd);
        result = 1;
    }

    return result;
}

/**
 * \test DetectFlowTestParseNocase02 is a test for setting the established flow opt
 */
int DetectFlowTestParseNocase02 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("ESTABLISHED");
    if (fd != NULL) {
        if (fd->flags == FLOW_PKT_ESTABLISHED && fd->match_cnt == 1) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_ESTABLISHED, 1, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParseNocase03 is a test for setting the stateless flow opt
 */
int DetectFlowTestParseNocase03 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("STATELESS");
    if (fd != NULL) {
        if (fd->flags == FLOW_PKT_STATELESS && fd->match_cnt == 1) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_STATELESS, 1, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParseNocase04 is a test for setting the to_client flow opt
 */
int DetectFlowTestParseNocase04 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("TO_CLIENT");
    if (fd != NULL) {
        if (fd->flags == FLOW_PKT_TOCLIENT && fd->match_cnt == 1) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_TOCLIENT, 1, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParseNocase05 is a test for setting the to_server flow opt
 */
int DetectFlowTestParseNocase05 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("TO_SERVER");
    if (fd != NULL) {
        if (fd->flags == FLOW_PKT_TOSERVER && fd->match_cnt == 1) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_TOSERVER, 1, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParseNocase06 is a test for setting the from_server flow opt
 */
int DetectFlowTestParseNocase06 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("FROM_SERVER");
    if (fd != NULL) {
        if (fd->flags == FLOW_PKT_TOCLIENT && fd->match_cnt == 1) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_TOCLIENT, 1, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParseNocase07 is a test for setting the from_client flow opt
 */
int DetectFlowTestParseNocase07 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("FROM_CLIENT");
    if (fd != NULL) {
        if (fd->flags == FLOW_PKT_TOSERVER && fd->match_cnt == 1) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_TOSERVER, 1, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParseNocase08 is a test for setting the established,to_client flow opts
 */
int DetectFlowTestParseNocase08 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("ESTABLISHED,TO_CLIENT");
    if (fd != NULL) {
        if (fd->flags & FLOW_PKT_ESTABLISHED && fd->flags & FLOW_PKT_TOCLIENT && fd->match_cnt == 2) {
            result = 1;
        } else {
            printf("expected: 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_ESTABLISHED + FLOW_PKT_TOCLIENT, 2, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParseNocase09 is a test for setting the to_client,stateless flow opts (order of state,dir reversed)
 */
int DetectFlowTestParseNocase09 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("TO_CLIENT,STATELESS");
    if (fd != NULL) {
        if (fd->flags & FLOW_PKT_STATELESS && fd->flags & FLOW_PKT_TOCLIENT && fd->match_cnt == 2) {
            result = 1;
        } else {
            printf("expected: 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_STATELESS + FLOW_PKT_TOCLIENT, 2, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParseNocase10 is a test for setting the from_server,stateless flow opts (order of state,dir reversed)
 */
int DetectFlowTestParseNocase10 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("FROM_SERVER,STATELESS");
    if (fd != NULL) {
        if (fd->flags & FLOW_PKT_STATELESS  && fd->flags & FLOW_PKT_TOCLIENT && fd->match_cnt == 2){
            result = 1;
        } else {
            printf("expected: 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_STATELESS + FLOW_PKT_TOCLIENT, 2, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParseNocase11 is a test for setting the from_server,stateless flow opts with spaces all around
 */
int DetectFlowTestParseNocase11 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse(" FROM_SERVER , STATELESS ");
    if (fd != NULL) {
        if (fd->flags & FLOW_PKT_STATELESS  && fd->flags & FLOW_PKT_TOCLIENT && fd->match_cnt == 2){
            result = 1;
        } else {
            printf("expected: 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_STATELESS + FLOW_PKT_TOCLIENT, 2, fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}


/**
 * \test DetectFlowTestParse12 is a test for setting an invalid seperator :
 */
int DetectFlowTestParse12 (void) {
    int result = 1;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("from_server:stateless");
    if (fd != NULL) {
        printf("expected: NULL got 0x%02X %" PRId32 ": ",fd->flags, fd->match_cnt);
        result = 0;
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse13 is a test for an invalid option
 */
int DetectFlowTestParse13 (void) {
    int result = 1;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("invalidoptiontest");
    if (fd != NULL) {
        printf("expected: NULL got 0x%02X %" PRId32 ": ",fd->flags, fd->match_cnt);
        result = 0;
        DetectFlowFree(fd);
    }

    return result;
}
/**
 * \test DetectFlowTestParse14 is a test for a empty option
 */
int DetectFlowTestParse14 (void) {
    int result = 1;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("");
    if (fd != NULL) {
        printf("expected: NULL got 0x%02X %" PRId32 ": ",fd->flags, fd->match_cnt);
        result = 0;
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse15 is a test for an invalid combo of options established,stateless
 */
int DetectFlowTestParse15 (void) {
    int result = 1;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("established,stateless");
    if (fd != NULL) {
        printf("expected: NULL got 0x%02X %" PRId32 ": ",fd->flags, fd->match_cnt);
        result = 0;
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse16 is a test for an invalid combo of options to_client,to_server
 */
int DetectFlowTestParse16 (void) {
    int result = 1;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("to_client,to_server");
    if (fd != NULL) {
        printf("expected: NULL got 0x%02X %" PRId32 ": ",fd->flags, fd->match_cnt);
        result = 0;
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse16 is a test for an invalid combo of options to_client,from_server
 * flowbit flags are the same
 */
int DetectFlowTestParse17 (void) {
    int result = 1;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("to_client,from_server");
    if (fd != NULL) {
        printf("expected: NULL got 0x%02X %" PRId32 ": ",fd->flags, fd->match_cnt);
        result = 0;
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse18 is a test for setting the from_server,stateless,stream_only flow opts (order of state,dir reversed)
 */
int DetectFlowTestParse18 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("from_server,established,stream_only");
    if (fd != NULL) {
        if (fd->flags & FLOW_PKT_ESTABLISHED && fd->flags & FLOW_PKT_TOCLIENT && fd->flags & FLOW_PKT_STREAMONLY && fd->match_cnt == 3) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_ESTABLISHED + FLOW_PKT_TOCLIENT + FLOW_PKT_STREAMONLY, 3,
                    fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParseNocase18 is a test for setting the from_server,stateless,stream_only flow opts (order of state,dir reversed)
 */
int DetectFlowTestParseNocase18 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("FROM_SERVER,ESTABLISHED,STREAM_ONLY");
    if (fd != NULL) {
        if (fd->flags & FLOW_PKT_ESTABLISHED && fd->flags & FLOW_PKT_TOCLIENT && fd->flags & FLOW_PKT_STREAMONLY && fd->match_cnt == 3) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_ESTABLISHED + FLOW_PKT_TOCLIENT + FLOW_PKT_STREAMONLY, 3,
                    fd->flags, fd->match_cnt);
        }
        DetectFlowFree(fd);
    }

    return result;
}


/**
 * \test DetectFlowTestParse19 is a test for one to many options passed to DetectFlowParse
 */
int DetectFlowTestParse19 (void) {
    int result = 1;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("from_server,established,stream_only,a");
    if (fd != NULL) {
        printf("expected: NULL got 0x%02X %" PRId32 ": ",fd->flags, fd->match_cnt);
        result = 0;
        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse20 is a test for setting from_server, established, no_stream
 */
int DetectFlowTestParse20 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("from_server,established,no_stream");
    if (fd != NULL) {
        if (fd->flags & FLOW_PKT_ESTABLISHED && fd->flags & FLOW_PKT_TOCLIENT && fd->flags & FLOW_PKT_NOSTREAM && fd->match_cnt == 3) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_ESTABLISHED + FLOW_PKT_TOCLIENT + FLOW_PKT_NOSTREAM, 3,
                    fd->flags, fd->match_cnt);
        }

        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse20 is a test for setting from_server, established, no_stream
 */
int DetectFlowTestParseNocase20 (void) {
    int result = 0;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("FROM_SERVER,ESTABLISHED,NO_STREAM");
    if (fd != NULL) {
        if (fd->flags & FLOW_PKT_ESTABLISHED && fd->flags & FLOW_PKT_TOCLIENT && fd->flags & FLOW_PKT_NOSTREAM && fd->match_cnt == 3) {
            result = 1;
        } else {
            printf("expected 0x%02X cnt %" PRId32 " got 0x%02X cnt %" PRId32 ": ", FLOW_PKT_ESTABLISHED + FLOW_PKT_TOCLIENT + FLOW_PKT_NOSTREAM, 3,
                    fd->flags, fd->match_cnt);
        }

        DetectFlowFree(fd);
    }

    return result;
}

/**
 * \test DetectFlowTestParse21 is a test for an invalid opt between to valid opts
 */
int DetectFlowTestParse21 (void) {
    int result = 1;
    DetectFlowData *fd = NULL;
    fd = DetectFlowParse("from_server,a,no_stream");
    if (fd != NULL) {
        printf("expected: NULL got 0x%02X %" PRId32 ": ",fd->flags, fd->match_cnt);
        result = 0;
        DetectFlowFree(fd);
    }

    return result;
}
#endif /* UNITTESTS */

/**
 * \brief this function registers unit tests for DetectFlow
 */
void DetectFlowRegisterTests(void) {
#ifdef UNITTESTS
    UtRegisterTest("DetectFlowTestParse01", DetectFlowTestParse01, 1);
    UtRegisterTest("DetectFlowTestParse02", DetectFlowTestParse02, 1);
    UtRegisterTest("DetectFlowTestParse03", DetectFlowTestParse03, 1);
    UtRegisterTest("DetectFlowTestParse04", DetectFlowTestParse04, 1);
    UtRegisterTest("DetectFlowTestParse05", DetectFlowTestParse05, 1);
    UtRegisterTest("DetectFlowTestParse06", DetectFlowTestParse06, 1);
    UtRegisterTest("DetectFlowTestParse07", DetectFlowTestParse07, 1);
    UtRegisterTest("DetectFlowTestParse08", DetectFlowTestParse08, 1);
    UtRegisterTest("DetectFlowTestParse09", DetectFlowTestParse09, 1);
    UtRegisterTest("DetectFlowTestParse10", DetectFlowTestParse10, 1);
    UtRegisterTest("DetectFlowTestParse11", DetectFlowTestParse11, 1);
    UtRegisterTest("DetectFlowTestParseNocase01", DetectFlowTestParseNocase01, 1);
    UtRegisterTest("DetectFlowTestParseNocase02", DetectFlowTestParseNocase02, 1);
    UtRegisterTest("DetectFlowTestParseNocase03", DetectFlowTestParseNocase03, 1);
    UtRegisterTest("DetectFlowTestParseNocase04", DetectFlowTestParseNocase04, 1);
    UtRegisterTest("DetectFlowTestParseNocase05", DetectFlowTestParseNocase05, 1);
    UtRegisterTest("DetectFlowTestParseNocase06", DetectFlowTestParseNocase06, 1);
    UtRegisterTest("DetectFlowTestParseNocase07", DetectFlowTestParseNocase07, 1);
    UtRegisterTest("DetectFlowTestParseNocase08", DetectFlowTestParseNocase08, 1);
    UtRegisterTest("DetectFlowTestParseNocase09", DetectFlowTestParseNocase09, 1);
    UtRegisterTest("DetectFlowTestParseNocase10", DetectFlowTestParseNocase10, 1);
    UtRegisterTest("DetectFlowTestParseNocase11", DetectFlowTestParseNocase11, 1);
    UtRegisterTest("DetectFlowTestParse12", DetectFlowTestParse12, 1);
    UtRegisterTest("DetectFlowTestParse13", DetectFlowTestParse13, 1);
    UtRegisterTest("DetectFlowTestParse14", DetectFlowTestParse14, 1);
    UtRegisterTest("DetectFlowTestParse15", DetectFlowTestParse15, 1);
    UtRegisterTest("DetectFlowTestParse16", DetectFlowTestParse16, 1);
    UtRegisterTest("DetectFlowTestParse17", DetectFlowTestParse17, 1);
    UtRegisterTest("DetectFlowTestParse18", DetectFlowTestParse18, 1);
    UtRegisterTest("DetectFlowTestParseNocase18", DetectFlowTestParseNocase18, 1);
    UtRegisterTest("DetectFlowTestParse19", DetectFlowTestParse19, 1);
    UtRegisterTest("DetectFlowTestParse20", DetectFlowTestParse20, 1);
    UtRegisterTest("DetectFlowTestParseNocase20", DetectFlowTestParseNocase20, 1);
    UtRegisterTest("DetectFlowTestParse21", DetectFlowTestParse21, 1);
#endif /* UNITTESTS */
}
