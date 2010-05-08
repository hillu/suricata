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
 * signature parser
 */

#include "suricata-common.h"
#include "debug.h"

#include "detect.h"
#include "detect-engine.h"
#include "detect-engine-address.h"
#include "detect-engine-port.h"
#include "detect-engine-mpm.h"

#include "detect-content.h"
#include "detect-uricontent.h"
#include "detect-reference.h"

#include "flow.h"

#include "util-rule-vars.h"
#include "conf.h"
#include "conf-yaml-loader.h"

#include "app-layer-protos.h"
#include "app-layer-parser.h"

#include "util-classification-config.h"
#include "util-unittest.h"
#include "util-unittest-helper.h"
#include "util-debug.h"
#include "string.h"
#include "detect-parse.h"
#include "detect-engine-iponly.h"

extern int sc_set_caps;

static pcre *config_pcre = NULL;
static pcre *option_pcre = NULL;
static pcre_extra *config_pcre_extra = NULL;
static pcre_extra *option_pcre_extra = NULL;

static uint32_t dbg_srcportany_cnt = 0;
static uint32_t dbg_dstportany_cnt = 0;

#define CONFIG_PARTS 8

#define CONFIG_ACTION 0
#define CONFIG_PROTO  1
#define CONFIG_SRC    2
#define CONFIG_SP     3
#define CONFIG_DIREC  4
#define CONFIG_DST    5
#define CONFIG_DP     6
#define CONFIG_OPTS   7

//                    action       protocol       src                                      sp                        dir              dst                                    dp                            options
#define CONFIG_PCRE "^([A-z]+)\\s+([A-z0-9]+)\\s+([\\[\\]A-z0-9\\.\\:_\\$\\!\\-,\\/]+)\\s+([\\:A-z0-9_\\$\\!,]+)\\s+(-\\>|\\<\\>)\\s+([\\[\\]A-z0-9\\.\\:_\\$\\!\\-,/]+)\\s+([\\:A-z0-9_\\$\\!,]+)(?:\\s+\\((.*)?(?:\\s*)\\))?(?:(?:\\s*)\\n)?$"
#define OPTION_PARTS 3
#define OPTION_PCRE "^\\s*([A-z_0-9-\\.]+)(?:\\s*\\:\\s*(.*)(?<!\\\\))?\\s*;\\s*(?:\\s*(.*))?\\s*$"

uint32_t DbgGetSrcPortAnyCnt(void) {
    return dbg_srcportany_cnt;
}

uint32_t DbgGetDstPortAnyCnt(void) {
    return dbg_dstportany_cnt;
}

SigMatch *SigMatchAlloc(void) {
    SigMatch *sm = SCMalloc(sizeof(SigMatch));
    if (sm == NULL)
        return NULL;

    memset(sm, 0, sizeof(SigMatch));
    sm->prev = NULL;
    sm->next = NULL;
    return sm;
}

/** \brief free a SigMatch
 *  \param sm SigMatch to free.
 */
void SigMatchFree(SigMatch *sm) {
    if (sm == NULL)
        return;

    /** free the ctx, for that we call the Free func */
    if (sm->ctx != NULL) {
        if (sigmatch_table[sm->type].Free != NULL) {
            sigmatch_table[sm->type].Free(sm->ctx);
        }
    }
    SCFree(sm);
}

/* Get the detection module by name */
SigTableElmt *SigTableGet(char *name) {
    SigTableElmt *st = NULL;
    int i = 0;

    for (i = 0; i < DETECT_TBLSIZE; i++) {
        st = &sigmatch_table[i];

        if (st->name != NULL) {
            if (strcasecmp(name,st->name) == 0)
                return st;
        }
    }

    return NULL;
}

/**
 * \brief SigMatchAppendUricontent, append a SigMatch of type uricontent
 *        to the Signature structure
 * \param s pointer to the Signature
 * \param new pointer to the SigMatch of type uricontent to be appended
 */
void SigMatchAppendUricontent(Signature *s, SigMatch *new) {
    if (s->umatch == NULL) {
        s->umatch = new;
        s->umatch_tail = new;
        new->next = NULL;
        new->prev = NULL;
    } else {
        SigMatch *cur = s->umatch_tail;
        cur->next = new;
        new->prev = cur;
        new->next = NULL;
        s->umatch_tail = new;
    }

    new->idx = s->sm_cnt;
    s->sm_cnt++;
}

void SigMatchAppendPayload(Signature *s, SigMatch *new) {
    if (s->pmatch == NULL) {
        s->pmatch = new;
        s->pmatch_tail = new;
        new->next = NULL;
        new->prev = NULL;
    } else {
        SigMatch *cur = s->pmatch_tail;
        cur->next = new;
        new->prev = cur;
        new->next = NULL;
        s->pmatch_tail = new;
    }

    new->idx = s->sm_cnt;
    s->sm_cnt++;
}

/** \brief Append a sig match to the signatures non-payload match list
 *
 *  \param s signature
 *  \param new sigmatch to append
 */
void SigMatchAppendPacket(Signature *s, SigMatch *new) {
    if (s->match == NULL) {
        s->match = new;
        s->match_tail = new;
        new->next = NULL;
        new->prev = NULL;
    } else {
        SigMatch *cur = s->match;

        for ( ; cur->next != NULL; cur = cur->next);

        cur->next = new;
        new->next = NULL;
        new->prev = cur;
        s->match_tail = new;
    }

    new->idx = s->sm_cnt;
    s->sm_cnt++;
}

/** \brief Pull a content 'old' from the pmatch list, append 'new' to match list.
  * Used for replacing contents that have http_cookie, etc modifiers.
  */
void SigMatchReplaceContent(Signature *s, SigMatch *old, SigMatch *new) {
    BUG_ON(old == NULL);

    SigMatch *m = s->pmatch;
    SigMatch *pm = m;

    for ( ; m != NULL; m = m->next) {
        if (m == old) {
            if (m == s->pmatch) {
                s->pmatch = m->next;
                if (m->next != NULL) {
                    m->next->prev = NULL;
                }
            } else {
                pm->next = m->next;
                if (m->next != NULL) {
                    m->next->prev = pm;
                }
            }

            if (m == s->pmatch_tail) {
                if (pm == m) {
                    s->pmatch_tail = NULL;
                } else {
                    s->pmatch_tail = pm;
                }
            }

            //printf("m %p  s->pmatch %p s->pmatch_tail %p\n", m, s->pmatch, s->pmatch_tail);
            break;
        }

        pm = m;
    }

    /* finally append the "new" sig match to the app layer list */
    /** \todo if the app layer gets it's own list, adapt this code */
    if (s->match == NULL) {
        s->match = new;
        s->match_tail = new;
        new->next = NULL;
        new->prev = NULL;
    } else {
        SigMatch *cur = s->match;

        for ( ; cur->next != NULL; cur = cur->next);

        cur->next = new;
        new->next = NULL;
        new->prev = cur;
        s->match_tail = new;
    }

    /* move over the idx */
    if (pm != NULL)
        new->idx = pm->idx;
}

/**
 * \brief Replaces the old sigmatch with the new sigmatch in the current
 *        signature.
 *
 * \param s     pointer to the current signature
 * \param m     pointer to the old sigmatch
 * \param new   pointer to the new sigmatch, which will replace m
 */
void SigMatchReplace(Signature *s, SigMatch *m, SigMatch *new) {
    if (s->match == NULL) {
        s->match = new;
        return;
    }

    if (m == NULL) {
        s->match = new;
    } else if (m->prev == NULL) {
        if (m->next != NULL) {
            m->next->prev = new;
            new->next = m->next;
        }
        s->match = new;
    } else {
        m->prev->next = new;
        new->prev = m->prev;
        if (m->next != NULL) {
            m->next->prev = new;
            new->next = m->next;
        }
    }
}

/**
 * \brief Returns a pointer to the last SigMatch instance of a particular type
 *        in a Signature of the payload list.
 *
 * \param s    Pointer to the tail of the sigmatch list
 * \param type SigMatch type which has to be searched for in the Signature.
 *
 * \retval match Pointer to the last SigMatch instance of type 'type'.
 */
SigMatch *SigMatchGetLastSM(SigMatch *sm, uint8_t type)
{
    while (sm != NULL) {
        if (sm->type == type) {
            return sm;
        }
        sm = sm->prev;
    }

    return NULL;
}

void SigParsePrepare(void) {
    char *regexstr = CONFIG_PCRE;
    const char *eb;
    int eo;
    int opts = 0;

    opts |= PCRE_UNGREEDY;
    config_pcre = pcre_compile(regexstr, opts, &eb, &eo, NULL);
    if(config_pcre == NULL)
    {
        SCLogError(SC_ERR_PCRE_COMPILE, "pcre compile of \"%s\" failed at offset %" PRId32 ": %s", regexstr, eo, eb);
        exit(1);
    }

    config_pcre_extra = pcre_study(config_pcre, 0, &eb);
    if(eb != NULL)
    {
        SCLogError(SC_ERR_PCRE_STUDY, "pcre study failed: %s", eb);
        exit(1);
    }

    regexstr = OPTION_PCRE;
    opts |= PCRE_UNGREEDY;

    option_pcre = pcre_compile(regexstr, opts, &eb, &eo, NULL);
    if(option_pcre == NULL)
    {
        SCLogError(SC_ERR_PCRE_COMPILE, "pcre compile of \"%s\" failed at offset %" PRId32 ": %s", regexstr, eo, eb);
        exit(1);
    }

    option_pcre_extra = pcre_study(option_pcre, 0, &eb);
    if(eb != NULL)
    {
        SCLogError(SC_ERR_PCRE_STUDY, "pcre study failed: %s", eb);
        exit(1);
    }
}

static int SigParseOptions(DetectEngineCtx *de_ctx, Signature *s, char *optstr) {
#define MAX_SUBSTRINGS 30
    int ov[MAX_SUBSTRINGS];
    int ret = 0, i = 0;
    SigTableElmt *st = NULL;
    char *optname = NULL, *optvalue = NULL, *optmore = NULL;

    const char **arr = SCCalloc(OPTION_PARTS+1, sizeof(char *));
    if (arr == NULL)
        return -1;

    ret = pcre_exec(option_pcre, option_pcre_extra, optstr, strlen(optstr), 0, 0, ov, MAX_SUBSTRINGS);
    /* if successful, we either have:
     *  2: keyword w/o value
     *  3: keyword w value, final opt OR keyword w/o value, more options coming
     *  4: keyword w value, more options coming
     */
    if (ret != 2 && ret != 3 && ret != 4) {
        SCLogError(SC_ERR_PCRE_MATCH, "pcre_exec failed: ret %" PRId32 ", optstr \"%s\"", ret, optstr);
        goto error;
    }

    /* extract the substrings */
    for (i = 1; i <= ret-1; i++) {
        pcre_get_substring(optstr, ov, MAX_SUBSTRINGS, i, &arr[i-1]);
        //printf("SigParseOptions: arr[%" PRId32 "] = \"%s\"\n", i-1, arr[i-1]);
    }
    arr[i-1]=NULL;

    /* Call option parsing */
    st = SigTableGet((char *)arr[0]);
    if (st == NULL) {
        SCLogError(SC_ERR_RULE_KEYWORD_UNKNOWN, "unknown rule keyword '%s'.", (char *)arr[0]);
        goto error;
    }

    if (st->flags & SIGMATCH_NOOPT) {
        optname = (char *)arr[0];
        optvalue = NULL;
        if (ret == 3) optmore = (char *)arr[1];
        else if (ret == 4) optmore = (char *)arr[2];
        else optmore = NULL;
    } else {
        optname = (char *)arr[0];
        optvalue = (char *)arr[1];
        if (ret == 4) optmore = (char *)arr[2];
        else optmore = NULL;
    }

    /* setup may or may not add a new SigMatch to the list */
    if (st->Setup(de_ctx, s, optvalue) < 0) {
        SCLogDebug("\"%s\" failed to setup", st->name);
        goto error;
    }

    if (ret == 4 && optmore != NULL) {
        //printf("SigParseOptions: recursive call for more options... (s:%p,m:%p)\n", s, m);

        if (optname) pcre_free_substring(optname);
        if (optvalue) pcre_free_substring(optvalue);
        if (optstr) SCFree(optstr);
        //if (optmore) pcre_free_substring(optmore);
        if (arr != NULL) SCFree(arr);
        return SigParseOptions(de_ctx, s, optmore);
    }

    if (optname) pcre_free_substring(optname);
    if (optvalue) pcre_free_substring(optvalue);
    if (optmore) pcre_free_substring(optmore);
    if (optstr) SCFree(optstr);
    if (arr != NULL) SCFree(arr);
    return 0;

error:
    if (optname) pcre_free_substring(optname);
    if (optvalue) pcre_free_substring(optvalue);
    if (optmore) pcre_free_substring(optmore);
    if (optstr) SCFree(optstr);
    if (arr != NULL) SCFree(arr);
    return -1;
}

/* XXX implement this for real
 *
 */
int SigParseAddress(Signature *s, const char *addrstr, char flag)
{
    SCLogDebug("Address Group \"%s\" to be parsed now", addrstr);

    /* pass on to the address(list) parser */
    if (flag == 0) {
        if (strcasecmp(addrstr, "any") == 0)
            s->flags |= SIG_FLAG_SRC_ANY;

        if (DetectAddressParse(&s->src, (char *)addrstr) < 0)
            goto error;
    } else {
        if (strcasecmp(addrstr, "any") == 0)
            s->flags |= SIG_FLAG_DST_ANY;

        if (DetectAddressParse(&s->dst, (char *)addrstr) < 0)
            goto error;
    }

    return 0;

error:
    return -1;
}

/**
 * \brief Parses the protocol supplied by the Signature.
 *
 *        http://www.iana.org/assignments/protocol-numbers
 *
 * \param s        Pointer to the Signature instance to which the parsed
 *                 protocol has to be added.
 * \param protostr Pointer to the character string containing the protocol name.
 *
 * \retval  0 On successfully parsing the protocl sent as the argument.
 * \retval -1 On failure
 */
int SigParseProto(Signature *s, const char *protostr) {
    int r = DetectProtoParse(&s->proto, (char *)protostr);
    if (r < 0) {
        s->alproto = AppLayerGetProtoByName(protostr);
        if (s->alproto != ALPROTO_UNKNOWN) {
            /* indicate that the signature is app-layer */
            s->flags |= SIG_FLAG_APPLAYER;

            /* app layer is always TCP for now */
            s->proto.proto[IPPROTO_TCP / 8] |= 1 << (IPPROTO_TCP % 8);
            return 0;
        }

        return -1;
    }

    return 0;
}

/**
 * \brief Parses the port(source or destination) field, from a Signature.
 *
 * \param s       Pointer to the signature which has to be updated with the
 *                port information.
 * \param portstr Pointer to the character string containing the port info.
 * \param         Flag which indicates if the portstr received is src or dst
 *                port.  For src port: flag = 0, dst port: flag = 1.
 *
 * \retval  0 On success.
 * \retval -1 On failure.
 */
int SigParsePort(Signature *s, const char *portstr, char flag)
{
    int r = 0;

    /* XXX VJ exclude handling this for none UDP/TCP proto's */

    SCLogDebug("Port group \"%s\" to be parsed", portstr);

    if (flag == 0) {
        if (strcasecmp(portstr, "any") == 0)
            s->flags |= SIG_FLAG_SP_ANY;

        r = DetectPortParse(&s->sp, (char *)portstr);
    } else if (flag == 1) {
        if (strcasecmp(portstr, "any") == 0)
            s->flags |= SIG_FLAG_DP_ANY;

        r = DetectPortParse(&s->dp, (char *)portstr);
    }

    if (r < 0)
        return -1;

    return 0;
}

/** \retval 1 valid
 *  \retval 0 invalid
 */
static int SigParseActionRejectValidate(void) {
#ifdef HAVE_LIBNET11
#ifdef HAVE_LIBCAP_NG
    if (sc_set_caps == TRUE) {
        SCLogError(SC_ERR_LIBNET11_INCOMPATIBLE_WITH_LIBCAP_NG, "Libnet 1.1 is "
            "incompatible with POSIX based capabilities with privs dropping. "
            "For rejects to work, run as root/super user.");
        return 0;
    }
#endif
#else /* no libnet 1.1 */
    SCLogError(SC_ERR_LIBNET_REQUIRED_FOR_ACTION, "Libnet 1.1.x is "
            "required for action \"%s\" but is not compiled into Suricata",
            action);
    return 0;
#endif
    return 1;
}

/**
 * \brief Parses the action that has been used by the Signature and allots it
 *        to its Signatue instance.
 *
 * \param s      Pointer to the Signatue instance to which the action belongs.
 * \param action Pointer to the action string used by the Signature.
 *
 * \retval  0 On successfully parsing the action string and adding it to the
 *            Signature.
 * \retval -1 On failure.
 */
int SigParseAction(Signature *s, const char *action) {
    if (strcasecmp(action, "alert") == 0) {
        s->action = ACTION_ALERT;
        return 0;
    } else if (strcasecmp(action, "drop") == 0) {
        s->action = ACTION_DROP;
        return 0;
    } else if (strcasecmp(action, "pass") == 0) {
        s->action = ACTION_PASS;
        return 0;
    } else if (strcasecmp(action, "reject") == 0) {
        if (!(SigParseActionRejectValidate()))
            return -1;
        s->action = ACTION_REJECT;
        return 0;
    } else if (strcasecmp(action, "rejectsrc") == 0) {
        if (!(SigParseActionRejectValidate()))
            return -1;
        s->action = ACTION_REJECT;
        return 0;
    } else if (strcasecmp(action, "rejectdst") == 0) {
        if (!(SigParseActionRejectValidate()))
            return -1;
        s->action = ACTION_REJECT_DST;
        return 0;
    } else if (strcasecmp(action, "rejectboth") == 0) {
        if (!(SigParseActionRejectValidate()))
            return -1;
        s->action = ACTION_REJECT_BOTH;
        return 0;
    } else {
        SCLogError(SC_ERR_INVALID_ACTION,"An invalid action \"%s\" was given",action);
        return -1;
    }
}

int SigParseBasics(Signature *s, char *sigstr, char ***result, uint8_t addrs_direction) {
#define MAX_SUBSTRINGS 30
    int ov[MAX_SUBSTRINGS];
    int ret = 0, i = 0;

    const char **arr = SCCalloc(CONFIG_PARTS + 1, sizeof(char *));
    if (arr == NULL)
        return -1;

    ret = pcre_exec(config_pcre, config_pcre_extra, sigstr, strlen(sigstr), 0, 0, ov, MAX_SUBSTRINGS);
    if (ret != 8 && ret != 9) {
        printf("SigParseBasics: pcre_exec failed: ret %" PRId32 ", sigstr \"%s\"\n", ret, sigstr);
        goto error;
    }

    for (i = 1; i <= ret - 1; i++) {
        pcre_get_substring(sigstr, ov, MAX_SUBSTRINGS, i, &arr[i - 1]);
        //printf("SigParseBasics: arr[%" PRId32 "] = \"%s\"\n", i-1, arr[i-1]);
    }
    arr[i - 1] = NULL;

    /* Parse Action */
    if (SigParseAction(s, arr[CONFIG_ACTION]) < 0)
        goto error;

    /* Parse Proto */
    if (SigParseProto(s, arr[CONFIG_PROTO]) < 0)
        goto error;

    /* Check if it is bidirectional */
    if (strcmp(arr[CONFIG_DIREC], "<>") == 0)
        s->flags |= SIG_FLAG_BIDIREC;

    /* Parse Address & Ports */
    if (SigParseAddress(s, arr[CONFIG_SRC], SIG_DIREC_SRC ^ addrs_direction) < 0)
       goto error;

    if (SigParseAddress(s, arr[CONFIG_DST], SIG_DIREC_DST ^ addrs_direction) < 0)
        goto error;

    /* For IPOnly */
    if (IPOnlySigParseAddress(s, arr[CONFIG_SRC], SIG_DIREC_SRC ^ addrs_direction) < 0)
       goto error;

    if (IPOnlySigParseAddress(s, arr[CONFIG_DST], SIG_DIREC_DST ^ addrs_direction) < 0)
        goto error;


    /* For "ip" we parse the ports as well, even though they will be just "any".
     *  We do this for later sgh building for the tcp and udp protocols. */
    if (DetectProtoContainsProto(&s->proto, IPPROTO_TCP) ||
        DetectProtoContainsProto(&s->proto, IPPROTO_UDP)) {
        if (SigParsePort(s, arr[CONFIG_SP], SIG_DIREC_SRC ^ addrs_direction) < 0)
            goto error;
        if (SigParsePort(s, arr[CONFIG_DP], SIG_DIREC_DST ^ addrs_direction) < 0)
            goto error;
    }

    *result = (char **)arr;
    return 0;

error:
    if (arr != NULL) {
        for (i = 1; i <= ret - 1; i++) {
            if (arr[i - 1] == NULL)
                continue;

            pcre_free_substring(arr[i - 1]);
        }
        SCFree(arr);
    }
    *result = NULL;
    return -1;
}

int SigParse(DetectEngineCtx *de_ctx, Signature *s, char *sigstr, uint8_t addrs_direction) {
    SCEnter();

    char **basics;

    int ret = SigParseBasics(s, sigstr, &basics, addrs_direction);
    if (ret < 0) {
        SCLogDebug("SigParseBasics failed");
        SCReturnInt(-1);
    }

#ifdef DEBUG
    if (SCLogDebugEnabled()) {
        int i;
        for (i = 0; basics[i] != NULL; i++) {
            SCLogDebug("basics[%" PRId32 "]: %p, %s", i, basics[i], basics[i]);
        }
    }
#endif /* DEBUG */

    /* we can have no options, so make sure we have them */
    if (basics[CONFIG_OPTS] != NULL) {
        ret = SigParseOptions(de_ctx, s, SCStrdup(basics[CONFIG_OPTS]));
        SCLogDebug("ret from SigParseOptions %d", ret);
    }

    /* cleanup */
    if (basics != NULL) {
        int i = 0;
        while (basics[i] != NULL) {
            SCFree(basics[i]);
            i++;
        }
        SCFree(basics);
    }

    SCReturnInt(ret);
}

Signature *SigAlloc (void) {
    Signature *sig = SCMalloc(sizeof(Signature));
    if (sig == NULL)
        return NULL;

    memset(sig, 0, sizeof(Signature));

    /* assign it to -1, so that we can later check if the value has been
     * overwritten after the Signature has been parsed, and if it hasn't been
     * overwritten, we can then assign the default value of 3 */
    sig->prio = -1;
    return sig;
}

/**
 * \internal
 * \brief Free Reference list
 *
 * \param s Pointer to the signature
 */
static void SigRefFree (Signature *s) {
    SCEnter();

    Reference *ref = NULL;
    Reference *next_ref = NULL;

    if (s == NULL) {
        SCReturn;
    }

    SCLogDebug("s %p, s->references %p", s, s->references);

    for (ref = s->references; ref != NULL;)   {
        next_ref = ref->next;
        DetectReferenceFree(ref);
        ref = next_ref;
    }

    s->references = NULL;

    SCReturn;
}

void SigFree(Signature *s) {
    if (s == NULL)
        return;

    SigMatch *sm = s->match, *nsm;
    while (sm != NULL) {
        nsm = sm->next;
        SigMatchFree(sm);
        sm = nsm;
    }

    sm = s->pmatch;
    while (sm != NULL) {
        nsm = sm->next;
        SigMatchFree(sm);
        sm = nsm;
    }
    DetectAddressHeadCleanup(&s->src);
    DetectAddressHeadCleanup(&s->dst);

    if (s->sp != NULL) {
        DetectPortCleanupList(s->sp);
    }
    if (s->dp != NULL) {
        DetectPortCleanupList(s->dp);
    }

    if (s->msg != NULL)
        SCFree(s->msg);

    SigRefFree(s);

    SCFree(s);
}

/**
 * \brief Parses a signature and adds it to the Detection Engine Context
 * This function is going to be deprecated. Should use DetectEngineAppendSig()
 * or SigInitReal() if you want to control the sig_list building.
 *
 * \param de_ctx Pointer to the Detection Engine Context
 * \param sigstr Pointer to a character string containing the signature to be
 *               parsed
 *
 * \retval Pointer to the Signature instance on success; NULL on failure
 */
Signature *SigInit(DetectEngineCtx *de_ctx, char *sigstr) {
    SCEnter();

    Signature *sig = SigAlloc();
    if (sig == NULL)
        goto error;

    if (SigParse(de_ctx, sig, sigstr, SIG_DIREC_NORMAL) < 0)
        goto error;

    /* signature priority hasn't been overwritten.  Using default priority */
    if (sig->prio == -1)
        sig->prio = 3;

    sig->num = de_ctx->signum;
    de_ctx->signum++;

    /* see if need to set the SIG_FLAG_MPM flag */
    SigMatch *sm;
    for (sm = sig->pmatch; sm != NULL; sm = sm->next) {
        if (sm->type == DETECT_CONTENT) {
            DetectContentData *cd = (DetectContentData *)sm->ctx;
            if (cd == NULL)
                continue;

            sig->flags |= SIG_FLAG_MPM;

            if (cd->flags & DETECT_CONTENT_NEGATED) {
                sig->flags |= SIG_FLAG_MPM_NEGCONTENT;
            }
        }
    }
    for (sm = sig->umatch; sm != NULL; sm = sm->next) {
        if (sm->type == DETECT_URICONTENT) {
            DetectUricontentData *ud = (DetectUricontentData *)sm->ctx;
            if (ud == NULL)
                continue;

            sig->flags |= SIG_FLAG_MPM;
        }
    }

    /* set mpm_content_len */

    /* determine the length of the longest pattern in the sig */
    if (sig->flags & SIG_FLAG_MPM) {
        sig->mpm_content_maxlen = 0;
        sig->mpm_uricontent_maxlen = 0;

        for (sm = sig->pmatch; sm != NULL; sm = sm->next) {
            if (sm->type == DETECT_CONTENT) {
                DetectContentData *cd = (DetectContentData *)sm->ctx;
                 if (cd == NULL)
                    continue;

                if (sig->mpm_content_maxlen == 0)
                    sig->mpm_content_maxlen = cd->content_len;
                if (sig->mpm_content_maxlen < cd->content_len)
                    sig->mpm_content_maxlen = cd->content_len;
            }
        }

        for (sm = sig->umatch; sm != NULL; sm = sm->next) {
            if (sm->type == DETECT_URICONTENT) {
                DetectUricontentData *ud = (DetectUricontentData *)sm->ctx;
                if (ud == NULL)
                    continue;

                if (sig->mpm_uricontent_maxlen == 0)
                    sig->mpm_uricontent_maxlen = ud->uricontent_len;
                if (sig->mpm_uricontent_maxlen < ud->uricontent_len)
                    sig->mpm_uricontent_maxlen = ud->uricontent_len;
            }
        }
    }

    /* set the packet and app layer flags, but only if the
     * app layer flag wasn't already set in which case we
     * only consider the app layer */
    if (!(sig->flags & SIG_FLAG_APPLAYER)) {
        if (sig->match != NULL) {
            SigMatch *sm = sig->match;
            for ( ; sm != NULL; sm = sm->next) {
                if (sigmatch_table[sm->type].AppLayerMatch != NULL)
                    sig->flags |= SIG_FLAG_APPLAYER;
                if (sigmatch_table[sm->type].Match != NULL)
                    sig->flags |= SIG_FLAG_PACKET;
            }
        } else {
            sig->flags |= SIG_FLAG_PACKET;
        }
    }

    SCLogDebug("sig %"PRIu32" SIG_FLAG_APPLAYER: %s, SIG_FLAG_PACKET: %s",
        sig->id, sig->flags & SIG_FLAG_APPLAYER ? "set" : "not set",
        sig->flags & SIG_FLAG_PACKET ? "set" : "not set");

    SCReturnPtr(sig, "Signature");

error:
    if ( sig != NULL ) SigFree(sig);
    if (de_ctx->failure_fatal == 1) {
        SCLogError(SC_ERR_INVALID_SIGNATURE,"Signature parsing failed: \"%s\"", sigstr);
        exit(EXIT_FAILURE);
    }
    SCReturnPtr(NULL,"Signature");
}

/**
 * \brief Parses a signature and assign a unique number from the Detection
 *        Engine Context. If the signature is bidirectional it should return
 *        two Signatures linked. Look if flag Bidirec is set and if the pointer
 *        sig->next is set (!= NULL).
 *
 * \param de_ctx Pointer to the Detection Engine Context
 * \param sigstr Pointer to a character string containing the signature to be
 *               parsed
 *
 * \retval Pointer to the Signature instance on success; NULL on failure
 */
Signature *SigInitReal(DetectEngineCtx *de_ctx, char *sigstr) {
    Signature *sig = SigAlloc();
    uint32_t oldsignum = de_ctx->signum;

    if (sig == NULL)
        goto error;

    /* XXX one day we will support this the way Snort does,
     * through classifications.config */
    sig->prio = 3;

    if (SigParse(de_ctx, sig, sigstr, SIG_DIREC_NORMAL) < 0)
        goto error;

    /* assign an unique id in this de_ctx */
    sig->num = de_ctx->signum;
    de_ctx->signum++;

    /* see if need to set the SIG_FLAG_MPM flag */
    SigMatch *sm;
    for (sm = sig->pmatch; sm != NULL; sm = sm->next) {
        if (sm->type == DETECT_CONTENT) {
            DetectContentData *cd = (DetectContentData *)sm->ctx;
            if (cd == NULL)
                continue;

            sig->flags |= SIG_FLAG_MPM;

            if (cd->flags & DETECT_CONTENT_NEGATED) {
                sig->flags |= SIG_FLAG_MPM_NEGCONTENT;
            }
        }
    }

    for (sm = sig->umatch; sm != NULL; sm = sm->next) {
        if (sm->type == DETECT_URICONTENT) {
            DetectUricontentData *ud = (DetectUricontentData *)sm->ctx;
            if (ud == NULL)
                continue;

            sig->flags |= SIG_FLAG_MPM;
        }
    }

    /* set mpm_content_len */

    /* determine the length of the longest pattern in the sig */
    if (sig->flags & SIG_FLAG_MPM) {
        sig->mpm_content_maxlen = 0;
        sig->mpm_uricontent_maxlen = 0;

        for (sm = sig->pmatch; sm != NULL; sm = sm->next) {
            if (sm->type == DETECT_CONTENT) {
                DetectContentData *cd = (DetectContentData *)sm->ctx;
                if (cd == NULL)
                    continue;

                if (sig->mpm_content_maxlen == 0)
                    sig->mpm_content_maxlen = cd->content_len;
                if (sig->mpm_content_maxlen < cd->content_len)
                    sig->mpm_content_maxlen = cd->content_len;
            }
        }

        for (sm = sig->umatch; sm != NULL; sm = sm->next) {
            if (sm->type == DETECT_URICONTENT) {
                DetectUricontentData *ud = (DetectUricontentData *)sm->ctx;
                if (ud == NULL)
                    continue;
                if (sig->mpm_uricontent_maxlen == 0)
                    sig->mpm_uricontent_maxlen = ud->uricontent_len;
                if (sig->mpm_uricontent_maxlen < ud->uricontent_len)
                    sig->mpm_uricontent_maxlen = ud->uricontent_len;
            }
        }
    }
    if (sig->flags & SIG_FLAG_BIDIREC) {
        /* Allocate a copy of this signature with the addresses siwtched
           This copy will be installed at sig->next */
        sig->next = SigAlloc();
        sig->next->prio = 3;
        if (sig->next == NULL)
            goto error;

        if (SigParse(de_ctx, sig->next, sigstr, SIG_DIREC_SWITCHED) < 0)
            goto error;
        /* assign an unique id in this de_ctx */
        sig->next->num = de_ctx->signum;
        de_ctx->signum++;

        /* set mpm_content_len */

        /* determine the length of the longest pattern in the sig */
        if (sig->next->flags & SIG_FLAG_MPM) {
            sig->next->mpm_content_maxlen = 0;
            sig->next->mpm_uricontent_maxlen = 0;

            SigMatch *sm;
            for (sm = sig->next->pmatch; sm != NULL; sm = sm->next) {
                if (sm->type == DETECT_CONTENT) {
                    DetectContentData *cd = (DetectContentData *)sm->ctx;
                    if (cd == NULL)
                        continue;

                    if (sig->next->mpm_content_maxlen == 0)
                        sig->next->mpm_content_maxlen = cd->content_len;
                    if (sig->next->mpm_content_maxlen < cd->content_len)
                        sig->next->mpm_content_maxlen = cd->content_len;
                }
            }
            for (sm = sig->next->umatch; sm != NULL; sm = sm->next) {
                if (sm->type == DETECT_URICONTENT) {
                    DetectUricontentData *ud = (DetectUricontentData *)sm->ctx;
                    if (ud == NULL)
                        continue;
                    if (sig->next->mpm_uricontent_maxlen == 0)
                        sig->next->mpm_uricontent_maxlen = ud->uricontent_len;
                    if (sig->next->mpm_uricontent_maxlen < ud->uricontent_len)
                        sig->next->mpm_uricontent_maxlen = ud->uricontent_len;
                }
            }
        }
    }

    /* set the packet and app layer flags, but only if the
     * app layer flag wasn't already set in which case we
     * only consider the app layer */
    if (!(sig->flags & SIG_FLAG_APPLAYER)) {
        if (sig->match != NULL) {
            SigMatch *sm = sig->match;
            for ( ; sm != NULL; sm = sm->next) {
                if (sigmatch_table[sm->type].AppLayerMatch != NULL)
                    sig->flags |= SIG_FLAG_APPLAYER;
                if (sigmatch_table[sm->type].Match != NULL)
                    sig->flags |= SIG_FLAG_PACKET;
            }
        } else {
            sig->flags |= SIG_FLAG_PACKET;
        }
    }

    SCLogDebug("sig %"PRIu32" SIG_FLAG_APPLAYER: %s, SIG_FLAG_PACKET: %s",
        sig->id, sig->flags & SIG_FLAG_APPLAYER ? "set" : "not set",
        sig->flags & SIG_FLAG_PACKET ? "set" : "not set");

    /**
     * In SigInitReal, the signature returned will point from the ptr next
     * to the cloned signatures with the switched addresses if it has
     * the bidirectional operator set
     */
    return sig;

error:
    if ( sig != NULL ) {
        if ( sig->next != NULL)
            SigFree(sig->next);
        SigFree(sig);
    }
    /* if something failed, restore the old signum count
     * since we didn't install it */
    de_ctx->signum = oldsignum;
    return NULL;
}

/**
 * \brief Parse and append a Signature into the Detection Engine Context
 *        signature list. If the signature is bidirectional it should append
 *        two Signatures (with the addresses switched).
 *
 * \param de_ctx Pointer to the Detection Engine Context
 * \param sigstr Pointer to a character string containing the signature to be
 *               parsed
 *
 * \retval Pointer to the head Signature in the detection engine ctx sig_list
 *               on success; NULL on failure
 */
Signature *DetectEngineAppendSig(DetectEngineCtx *de_ctx, char *sigstr) {
    Signature *sig = SigInitReal(de_ctx, sigstr);
    if (sig == NULL)
        return NULL;

    if (sig->flags & SIG_FLAG_BIDIREC) {
        if (sig->next != NULL) {
            sig->next->next = de_ctx->sig_list;
        } else {
            goto error;
        }
    }
    else {
        /* if this sig is the first one, sig_list should be null */
        sig->next = de_ctx->sig_list;
    }

    de_ctx->sig_list = sig;

    /**
     * In DetectEngineAppendSig(), the signatures are prepended and we always return the first one
     * so if the signature is bidirectional, the returned sig will point through "next" ptr
     * to the cloned signatures with the switched addresses
     */
    return sig;

error:
    if ( sig != NULL ) SigFree(sig);
    return NULL;
}

/*
 * TESTS
 */

#ifdef UNITTESTS
int SigParseTest01 (void) {
    int result = 1;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = SigInit(de_ctx, "alert tcp 1.2.3.4 any -> !1.2.3.4 any (msg:\"SigParseTest01\"; sid:1;)");
    if (sig == NULL)
        result = 0;

end:
    if (sig != NULL) SigFree(sig);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

int SigParseTest02 (void) {
    int result = 0;
    Signature *sig = NULL;
    DetectPort *port = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();

    if (de_ctx == NULL)
        goto end;

    SCClassConfGenerateValidDummyClassConfigFD01();
    SCClassConfLoadClassficationConfigFile(de_ctx);
    SCClassConfDeleteDummyClassificationConfigFD();

    sig = SigInit(de_ctx, "alert tcp any !21:902 -> any any (msg:\"ET MALWARE Suspicious 220 Banner on Local Port\"; content:\"220\"; offset:0; depth:4; pcre:\"/220[- ]/\"; sid:2003055; rev:4;)");
    if (sig == NULL) {
        goto end;
    }

    int r = DetectPortParse(&port, "0:20");
    if (r < 0)
        goto end;

    if (DetectPortCmp(sig->sp, port) == PORT_EQ) {
        result = 1;
    } else {
        DetectPortPrint(port); printf(" != "); DetectPortPrint(sig->sp); printf(": ");
    }

end:
    if (port != NULL) DetectPortCleanupList(port);
    if (sig != NULL) SigFree(sig);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test SigParseTest03 test for invalid direction operator in rule
 */
int SigParseTest03 (void) {
    int result = 1;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = SigInit(de_ctx, "alert tcp 1.2.3.4 any <- !1.2.3.4 any (msg:\"SigParseTest03\"; sid:1;)");
    if (sig != NULL) {
        result = 0;
        printf("expected NULL got sig ptr %p: ",sig);
    }

end:
    if (sig != NULL) SigFree(sig);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

int SigParseTest04 (void) {
    int result = 1;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = SigInit(de_ctx, "alert tcp 1.2.3.4 1024: -> !1.2.3.4 1024: (msg:\"SigParseTest04\"; sid:1;)");
    if (sig == NULL)
        result = 0;

end:
    if (sig != NULL) SigFree(sig);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Port validation */
int SigParseTest05 (void) {
    int result = 0;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = SigInit(de_ctx, "alert tcp 1.2.3.4 1024:65536 -> !1.2.3.4 any (msg:\"SigParseTest05\"; sid:1;)");
    if (sig == NULL) {
        result = 1;
    } else {
        printf("signature didn't fail to parse as we expected: ");
    }

end:
    if (sig != NULL) SigFree(sig);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Parsing bug debugging at 2010-03-18 */
int SigParseTest06 (void) {
    int result = 0;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = SigInit(de_ctx, "alert tcp any any -> any any (flow:to_server; content:\"GET\"; nocase; http_method; uricontent:\"/uri/\"; nocase; content:\"Host|3A| abc\"; nocase; sid:1; rev:1;)");
    if (sig != NULL) {
        result = 1;
    } else {
        printf("signature failed to parse: ");
    }

end:
    if (sig != NULL) SigFree(sig);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Direction operator validation (invalid) */
int SigParseBidirecTest06 (void) {
    int result = 1;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = DetectEngineAppendSig(de_ctx, "alert tcp 192.168.1.1 any - 192.168.1.5 any (msg:\"SigParseBidirecTest05\"; sid:1;)");
    if (sig == NULL)
        result = 1;

end:
    if (sig != NULL) SigFree(sig);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Direction operator validation (invalid) */
int SigParseBidirecTest07 (void) {
    int result = 1;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = DetectEngineAppendSig(de_ctx, "alert tcp 192.168.1.1 any <- 192.168.1.5 any (msg:\"SigParseBidirecTest05\"; sid:1;)");
    if (sig == NULL)
        result = 1;

end:
    if (sig != NULL) SigFree(sig);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Direction operator validation (invalid) */
int SigParseBidirecTest08 (void) {
    int result = 1;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = DetectEngineAppendSig(de_ctx, "alert tcp 192.168.1.1 any < 192.168.1.5 any (msg:\"SigParseBidirecTest05\"; sid:1;)");
    if (sig == NULL)
        result = 1;

end:
    if (sig != NULL) SigFree(sig);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Direction operator validation (invalid) */
int SigParseBidirecTest09 (void) {
    int result = 1;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = DetectEngineAppendSig(de_ctx, "alert tcp 192.168.1.1 any > 192.168.1.5 any (msg:\"SigParseBidirecTest05\"; sid:1;)");
    if (sig == NULL)
        result = 1;

end:
    if (sig != NULL) SigFree(sig);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Direction operator validation (invalid) */
int SigParseBidirecTest10 (void) {
    int result = 1;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = DetectEngineAppendSig(de_ctx, "alert tcp 192.168.1.1 any -< 192.168.1.5 any (msg:\"SigParseBidirecTest05\"; sid:1;)");
    if (sig == NULL)
        result = 1;

end:
    if (sig != NULL) SigFree(sig);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Direction operator validation (invalid) */
int SigParseBidirecTest11 (void) {
    int result = 1;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = DetectEngineAppendSig(de_ctx, "alert tcp 192.168.1.1 any >- 192.168.1.5 any (msg:\"SigParseBidirecTest05\"; sid:1;)");
    if (sig == NULL)
        result = 1;

end:
    if (sig != NULL) SigFree(sig);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Direction operator validation (invalid) */
int SigParseBidirecTest12 (void) {
    int result = 1;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = DetectEngineAppendSig(de_ctx, "alert tcp 192.168.1.1 any >< 192.168.1.5 any (msg:\"SigParseBidirecTest05\"; sid:1;)");
    if (sig == NULL)
        result = 1;

end:
    if (sig != NULL) SigFree(sig);
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Direction operator validation (valid) */
int SigParseBidirecTest13 (void) {
    int result = 1;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = DetectEngineAppendSig(de_ctx, "alert tcp 192.168.1.1 any <> 192.168.1.5 any (msg:\"SigParseBidirecTest05\"; sid:1;)");
    if (sig != NULL)
        result = 1;

end:
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Direction operator validation (valid) */
int SigParseBidirecTest14 (void) {
    int result = 1;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = DetectEngineAppendSig(de_ctx, "alert tcp 192.168.1.1 any -> 192.168.1.5 any (msg:\"SigParseBidirecTest05\"; sid:1;)");
    if (sig != NULL)
        result = 1;

end:
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/** \test Ensure that we don't set bidirectional in a
 *         normal (one direction) Signature
 */
int SigTestBidirec01 (void) {
    Signature *sig = NULL;
    int result = 0;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = DetectEngineAppendSig(de_ctx, "alert tcp 1.2.3.4 1024:65535 -> !1.2.3.4 any (msg:\"SigTestBidirec01\"; sid:1;)");
    if (sig == NULL)
        goto end;
    if (sig->next != NULL)
        goto end;
    if (sig->flags & SIG_FLAG_BIDIREC)
        goto end;
    if (de_ctx->signum != 1)
        goto end;

    result = 1;

end:
    if (de_ctx != NULL) {
        SigCleanSignatures(de_ctx);
        SigGroupCleanup(de_ctx);
        DetectEngineCtxFree(de_ctx);
    }
    return result;
}

/** \test Ensure that we set a bidirectional Signature correctly */
int SigTestBidirec02 (void) {
    int result = 0;
    Signature *sig = NULL;
    Signature *copy = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;

    sig = DetectEngineAppendSig(de_ctx, "alert tcp 1.2.3.4 1024:65535 <> !1.2.3.4 any (msg:\"SigTestBidirec02\"; sid:1;)");
    if (sig == NULL)
        goto end;
    if (de_ctx->sig_list != sig)
        goto end;
    if (!(sig->flags & SIG_FLAG_BIDIREC))
        goto end;
    if (sig->next == NULL)
        goto end;
    if (de_ctx->signum != 2)
        goto end;
    copy = sig->next;
    if (copy->next != NULL)
        goto end;
    if (!(copy->flags & SIG_FLAG_BIDIREC))
        goto end;

    result = 1;

end:
    if (de_ctx != NULL) {
        SigCleanSignatures(de_ctx);
        SigGroupCleanup(de_ctx);
        DetectEngineCtxFree(de_ctx);
    }

    return result;
}

/** \test Ensure that we set a bidirectional Signature correctly
*         and we install it with the rest of the signatures, checking
*         also that it match with the correct addr directions
*/
int SigTestBidirec03 (void) {
    int result = 0;
    Signature *sig = NULL;
    Packet *p = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;

    char *sigs[3];
    sigs[0] = "alert tcp any any -> 192.168.1.1 any (msg:\"SigTestBidirec03 sid 1\"; sid:1;)";
    sigs[1] = "alert tcp any any <> 192.168.1.1 any (msg:\"SigTestBidirec03 sid 2 bidirectional\"; sid:2;)";
    sigs[2] = "alert tcp any any -> 192.168.1.1 any (msg:\"SigTestBidirec03 sid 3\"; sid:3;)";
    UTHAppendSigs(de_ctx, sigs, 3);

    /* Checking that bidirectional rules are set correctly */
    sig = de_ctx->sig_list;
    if (sig == NULL)
        goto end;
    if (sig->next == NULL)
        goto end;
    if (sig->next->next == NULL)
        goto end;
    if (sig->next->next->next == NULL)
        goto end;
    if (sig->next->next->next->next != NULL)
        goto end;
    if (de_ctx->signum != 4)
        goto end;

    uint8_t rawpkt1_ether[] = {
        0x00,0x50,0x56,0xea,0x00,0xbd,0x00,0x0c,
        0x29,0x40,0xc8,0xb5,0x08,0x00,0x45,0x00,
        0x01,0xa8,0xb9,0xbb,0x40,0x00,0x40,0x06,
        0xe0,0xbf,0xc0,0xa8,0x1c,0x83,0xc0,0xa8,
        0x01,0x01,0xb9,0x0a,0x00,0x50,0x6f,0xa2,
        0x92,0xed,0x7b,0xc1,0xd3,0x4d,0x50,0x18,
        0x16,0xd0,0xa0,0x6f,0x00,0x00,0x47,0x45,
        0x54,0x20,0x2f,0x20,0x48,0x54,0x54,0x50,
        0x2f,0x31,0x2e,0x31,0x0d,0x0a,0x48,0x6f,
        0x73,0x74,0x3a,0x20,0x31,0x39,0x32,0x2e,
        0x31,0x36,0x38,0x2e,0x31,0x2e,0x31,0x0d,
        0x0a,0x55,0x73,0x65,0x72,0x2d,0x41,0x67,
        0x65,0x6e,0x74,0x3a,0x20,0x4d,0x6f,0x7a,
        0x69,0x6c,0x6c,0x61,0x2f,0x35,0x2e,0x30,
        0x20,0x28,0x58,0x31,0x31,0x3b,0x20,0x55,
        0x3b,0x20,0x4c,0x69,0x6e,0x75,0x78,0x20,
        0x78,0x38,0x36,0x5f,0x36,0x34,0x3b,0x20,
        0x65,0x6e,0x2d,0x55,0x53,0x3b,0x20,0x72,
        0x76,0x3a,0x31,0x2e,0x39,0x2e,0x30,0x2e,
        0x31,0x34,0x29,0x20,0x47,0x65,0x63,0x6b,
        0x6f,0x2f,0x32,0x30,0x30,0x39,0x30,0x39,
        0x30,0x32,0x31,0x37,0x20,0x55,0x62,0x75,
        0x6e,0x74,0x75,0x2f,0x39,0x2e,0x30,0x34,
        0x20,0x28,0x6a,0x61,0x75,0x6e,0x74,0x79,
        0x29,0x20,0x46,0x69,0x72,0x65,0x66,0x6f,
        0x78,0x2f,0x33,0x2e,0x30,0x2e,0x31,0x34,
        0x0d,0x0a,0x41,0x63,0x63,0x65,0x70,0x74,
        0x3a,0x20,0x74,0x65,0x78,0x74,0x2f,0x68,
        0x74,0x6d,0x6c,0x2c,0x61,0x70,0x70,0x6c,
        0x69,0x63,0x61,0x74,0x69,0x6f,0x6e,0x2f,
        0x78,0x68,0x74,0x6d,0x6c,0x2b,0x78,0x6d,
        0x6c,0x2c,0x61,0x70,0x70,0x6c,0x69,0x63,
        0x61,0x74,0x69,0x6f,0x6e,0x2f,0x78,0x6d,
        0x6c,0x3b,0x71,0x3d,0x30,0x2e,0x39,0x2c,
        0x2a,0x2f,0x2a,0x3b,0x71,0x3d,0x30,0x2e,
        0x38,0x0d,0x0a,0x41,0x63,0x63,0x65,0x70,
        0x74,0x2d,0x4c,0x61,0x6e,0x67,0x75,0x61,
        0x67,0x65,0x3a,0x20,0x65,0x6e,0x2d,0x75,
        0x73,0x2c,0x65,0x6e,0x3b,0x71,0x3d,0x30,
        0x2e,0x35,0x0d,0x0a,0x41,0x63,0x63,0x65,
        0x70,0x74,0x2d,0x45,0x6e,0x63,0x6f,0x64,
        0x69,0x6e,0x67,0x3a,0x20,0x67,0x7a,0x69,
        0x70,0x2c,0x64,0x65,0x66,0x6c,0x61,0x74,
        0x65,0x0d,0x0a,0x41,0x63,0x63,0x65,0x70,
        0x74,0x2d,0x43,0x68,0x61,0x72,0x73,0x65,
        0x74,0x3a,0x20,0x49,0x53,0x4f,0x2d,0x38,
        0x38,0x35,0x39,0x2d,0x31,0x2c,0x75,0x74,
        0x66,0x2d,0x38,0x3b,0x71,0x3d,0x30,0x2e,
        0x37,0x2c,0x2a,0x3b,0x71,0x3d,0x30,0x2e,
        0x37,0x0d,0x0a,0x4b,0x65,0x65,0x70,0x2d,
        0x41,0x6c,0x69,0x76,0x65,0x3a,0x20,0x33,
        0x30,0x30,0x0d,0x0a,0x43,0x6f,0x6e,0x6e,
        0x65,0x63,0x74,0x69,0x6f,0x6e,0x3a,0x20,
        0x6b,0x65,0x65,0x70,0x2d,0x61,0x6c,0x69,
        0x76,0x65,0x0d,0x0a,0x0d,0x0a }; /* end rawpkt1_ether */

    FlowInitConfig(FLOW_QUIET);
    p = UTHBuildPacketFromEth(rawpkt1_ether, sizeof(rawpkt1_ether));
    if (p == NULL) {
        SCLogDebug("Error building packet");
        goto end;
    }
    UTHMatchPackets(de_ctx, &p, 1);

    uint32_t sids[3] = {1, 2, 3};
    uint32_t results[3] = {1, 1, 1};
    result = UTHCheckPacketMatchResults(p, sids, results, 1);

end:
    if (p != NULL)
        SCFree(p);
    if (de_ctx != NULL) {
        SigCleanSignatures(de_ctx);
        SigGroupCleanup(de_ctx);
        DetectEngineCtxFree(de_ctx);
    }
    FlowShutdown();

    return result;
}

/** \test Ensure that we set a bidirectional Signature correctly
*         and we install it with the rest of the signatures, checking
*         also that it match with the correct addr directions
*/
int SigTestBidirec04 (void) {
    int result = 0;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;

    sig = DetectEngineAppendSig(de_ctx, "alert tcp 192.168.1.1 any -> any any (msg:\"SigTestBidirec03 sid 1\"; sid:1;)");
    if (sig == NULL)
        goto end;
    sig = DetectEngineAppendSig(de_ctx, "alert tcp 192.168.1.1 any <> any any (msg:\"SigTestBidirec03 sid 2 bidirectional\"; sid:2;)");
    if (sig == NULL)
        goto end;
    if ( !(sig->flags & SIG_FLAG_BIDIREC))
        goto end;
    if (sig->next == NULL)
        goto end;
    if (sig->next->next == NULL)
        goto end;
    if (sig->next->next->next != NULL)
        goto end;
    if (de_ctx->signum != 3)
        goto end;

    sig = DetectEngineAppendSig(de_ctx, "alert tcp 192.168.1.1 any -> any any (msg:\"SigTestBidirec03 sid 3\"; sid:3;)");
    if (sig == NULL)
        goto end;
    if (sig->next == NULL)
        goto end;
    if (sig->next->next == NULL)
        goto end;
    if (sig->next->next->next == NULL)
        goto end;
    if (sig->next->next->next->next != NULL)
        goto end;
    if (de_ctx->signum != 4)
        goto end;

    uint8_t rawpkt1_ether[] = {
        0x00,0x50,0x56,0xea,0x00,0xbd,0x00,0x0c,
        0x29,0x40,0xc8,0xb5,0x08,0x00,0x45,0x00,
        0x01,0xa8,0xb9,0xbb,0x40,0x00,0x40,0x06,
        0xe0,0xbf,0xc0,0xa8,0x1c,0x83,0xc0,0xa8,
        0x01,0x01,0xb9,0x0a,0x00,0x50,0x6f,0xa2,
        0x92,0xed,0x7b,0xc1,0xd3,0x4d,0x50,0x18,
        0x16,0xd0,0xa0,0x6f,0x00,0x00,0x47,0x45,
        0x54,0x20,0x2f,0x20,0x48,0x54,0x54,0x50,
        0x2f,0x31,0x2e,0x31,0x0d,0x0a,0x48,0x6f,
        0x73,0x74,0x3a,0x20,0x31,0x39,0x32,0x2e,
        0x31,0x36,0x38,0x2e,0x31,0x2e,0x31,0x0d,
        0x0a,0x55,0x73,0x65,0x72,0x2d,0x41,0x67,
        0x65,0x6e,0x74,0x3a,0x20,0x4d,0x6f,0x7a,
        0x69,0x6c,0x6c,0x61,0x2f,0x35,0x2e,0x30,
        0x20,0x28,0x58,0x31,0x31,0x3b,0x20,0x55,
        0x3b,0x20,0x4c,0x69,0x6e,0x75,0x78,0x20,
        0x78,0x38,0x36,0x5f,0x36,0x34,0x3b,0x20,
        0x65,0x6e,0x2d,0x55,0x53,0x3b,0x20,0x72,
        0x76,0x3a,0x31,0x2e,0x39,0x2e,0x30,0x2e,
        0x31,0x34,0x29,0x20,0x47,0x65,0x63,0x6b,
        0x6f,0x2f,0x32,0x30,0x30,0x39,0x30,0x39,
        0x30,0x32,0x31,0x37,0x20,0x55,0x62,0x75,
        0x6e,0x74,0x75,0x2f,0x39,0x2e,0x30,0x34,
        0x20,0x28,0x6a,0x61,0x75,0x6e,0x74,0x79,
        0x29,0x20,0x46,0x69,0x72,0x65,0x66,0x6f,
        0x78,0x2f,0x33,0x2e,0x30,0x2e,0x31,0x34,
        0x0d,0x0a,0x41,0x63,0x63,0x65,0x70,0x74,
        0x3a,0x20,0x74,0x65,0x78,0x74,0x2f,0x68,
        0x74,0x6d,0x6c,0x2c,0x61,0x70,0x70,0x6c,
        0x69,0x63,0x61,0x74,0x69,0x6f,0x6e,0x2f,
        0x78,0x68,0x74,0x6d,0x6c,0x2b,0x78,0x6d,
        0x6c,0x2c,0x61,0x70,0x70,0x6c,0x69,0x63,
        0x61,0x74,0x69,0x6f,0x6e,0x2f,0x78,0x6d,
        0x6c,0x3b,0x71,0x3d,0x30,0x2e,0x39,0x2c,
        0x2a,0x2f,0x2a,0x3b,0x71,0x3d,0x30,0x2e,
        0x38,0x0d,0x0a,0x41,0x63,0x63,0x65,0x70,
        0x74,0x2d,0x4c,0x61,0x6e,0x67,0x75,0x61,
        0x67,0x65,0x3a,0x20,0x65,0x6e,0x2d,0x75,
        0x73,0x2c,0x65,0x6e,0x3b,0x71,0x3d,0x30,
        0x2e,0x35,0x0d,0x0a,0x41,0x63,0x63,0x65,
        0x70,0x74,0x2d,0x45,0x6e,0x63,0x6f,0x64,
        0x69,0x6e,0x67,0x3a,0x20,0x67,0x7a,0x69,
        0x70,0x2c,0x64,0x65,0x66,0x6c,0x61,0x74,
        0x65,0x0d,0x0a,0x41,0x63,0x63,0x65,0x70,
        0x74,0x2d,0x43,0x68,0x61,0x72,0x73,0x65,
        0x74,0x3a,0x20,0x49,0x53,0x4f,0x2d,0x38,
        0x38,0x35,0x39,0x2d,0x31,0x2c,0x75,0x74,
        0x66,0x2d,0x38,0x3b,0x71,0x3d,0x30,0x2e,
        0x37,0x2c,0x2a,0x3b,0x71,0x3d,0x30,0x2e,
        0x37,0x0d,0x0a,0x4b,0x65,0x65,0x70,0x2d,
        0x41,0x6c,0x69,0x76,0x65,0x3a,0x20,0x33,
        0x30,0x30,0x0d,0x0a,0x43,0x6f,0x6e,0x6e,
        0x65,0x63,0x74,0x69,0x6f,0x6e,0x3a,0x20,
        0x6b,0x65,0x65,0x70,0x2d,0x61,0x6c,0x69,
        0x76,0x65,0x0d,0x0a,0x0d,0x0a }; /* end rawpkt1_ether */

    Packet p;
    DecodeThreadVars dtv;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx;

    memset(&th_v, 0, sizeof(th_v));
    memset(&p, 0, sizeof(p));

    FlowInitConfig(FLOW_QUIET);
    DecodeEthernet(&th_v, &dtv, &p, rawpkt1_ether, sizeof(rawpkt1_ether), NULL);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    /* At this point we have a list of 4 signatures. The last one
       is a copy of the second one. If we receive a packet
       with source 192.168.1.1 80, all the sids should match */

    SigGroupBuild(de_ctx);
    //PatternMatchPrepare(mpm_ctx, MPM_B2G);
    SigMatchSignatures(&th_v, de_ctx, det_ctx, &p);

    /* only sid 2 should match with a packet going to 192.168.1.1 port 80 */
    if (PacketAlertCheck(&p, 1) <= 0 && PacketAlertCheck(&p, 3) <= 0 &&
        PacketAlertCheck(&p, 2) == 1) {
        result = 1;
    }

    FlowShutdown();
    //PatternMatchDestroy(mpm_ctx);
    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);

end:
    if (de_ctx != NULL) {
        SigCleanSignatures(de_ctx);
        SigGroupCleanup(de_ctx);
        DetectEngineCtxFree(de_ctx);
    }

    return result;
}

/**
 * \test check that we don't allow invalid negation options
 */
static int SigParseTestNegation01 (void) {
    int result = 0;
    DetectEngineCtx *de_ctx;
    Signature *s=NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;
    de_ctx->flags |= DE_QUIET;

    s = SigInit(de_ctx,"alert tcp !any any -> any any (msg:\"SigTest41-01 src address is !any \"; classtype:misc-activity; sid:410001; rev:1;)");
    if (s != NULL) {
        SigFree(s);
        goto end;
    }

    result = 1;
end:
    if (de_ctx != NULL) DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test check that we don't allow invalid negation options
 */
static int SigParseTestNegation02 (void) {
    int result = 0;
    DetectEngineCtx *de_ctx;
    Signature *s=NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;
    de_ctx->flags |= DE_QUIET;

    s = SigInit(de_ctx,"alert tcp any !any -> any any (msg:\"SigTest41-02 src ip is !any \"; classtype:misc-activity; sid:410002; rev:1;)");
    if (s != NULL) {
        SigFree(s);
        goto end;
    }

    result = 1;
end:
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);
    return result;
}
/**
 * \test check that we don't allow invalid negation options
 */
static int SigParseTestNegation03 (void) {
    int result = 0;
    DetectEngineCtx *de_ctx;
    Signature *s=NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;
    de_ctx->flags |= DE_QUIET;

    s = SigInit(de_ctx,"alert tcp any any -> any [80:!80] (msg:\"SigTest41-03 dst port [80:!80] \"; classtype:misc-activity; sid:410003; rev:1;)");
    if (s != NULL) {
        SigFree(s);
        goto end;
    }

    result = 1;
end:
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);
    return result;
}
/**
 * \test check that we don't allow invalid negation options
 */
static int SigParseTestNegation04 (void) {
    int result = 0;
    DetectEngineCtx *de_ctx;
    Signature *s=NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;
    de_ctx->flags |= DE_QUIET;

    s = SigInit(de_ctx,"alert tcp any any -> any [80,!80] (msg:\"SigTest41-03 dst port [80:!80] \"; classtype:misc-activity; sid:410003; rev:1;)");
    if (s != NULL) {
        SigFree(s);
        goto end;
    }

    result = 1;
end:
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);
    return result;
}
/**
 * \test check that we don't allow invalid negation options
 */
static int SigParseTestNegation05 (void) {
    int result = 0;
    DetectEngineCtx *de_ctx;
    Signature *s=NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;
    de_ctx->flags |= DE_QUIET;

    s = SigInit(de_ctx,"alert tcp any any -> [192.168.0.2,!192.168.0.2] any (msg:\"SigTest41-04 dst ip [192.168.0.2,!192.168.0.2] \"; classtype:misc-activity; sid:410004; rev:1;)");
    if (s != NULL) {
        SigFree(s);
        goto end;
    }

    result = 1;
end:
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);
    return result;
}
/**
 * \test check that we don't allow invalid negation options
 */
static int SigParseTestNegation06 (void) {
    int result = 0;
    DetectEngineCtx *de_ctx;
    Signature *s=NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;
    de_ctx->flags |= DE_QUIET;

    s = SigInit(de_ctx,"alert tcp any any -> any [100:1000,!1:20000] (msg:\"SigTest41-05 dst port [100:1000,!1:20000] \"; classtype:misc-activity; sid:410005; rev:1;)");
    if (s != NULL) {
        SigFree(s);
        goto end;
    }

    result = 1;
end:
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test check that we don't allow invalid negation options
 */
static int SigParseTestNegation07 (void) {
    int result = 0;
    DetectEngineCtx *de_ctx;
    Signature *s=NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;
    de_ctx->flags |= DE_QUIET;

    s = SigInit(de_ctx,"alert tcp any any -> [192.168.0.2,!192.168.0.0/24] any (msg:\"SigTest41-06 dst ip [192.168.0.2,!192.168.0.0/24] \"; classtype:misc-activity; sid:410006; rev:1;)");
    if (s != NULL) {
        SigFree(s);
        goto end;
    }

    result = 1;
end:
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test mpm
 */
int SigParseTestMpm01 (void) {
    int result = 0;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = SigInit(de_ctx, "alert tcp any any -> any any (msg:\"mpm test\"; content:\"abcd\"; sid:1;)");
    if (sig == NULL) {
        printf("sig failed to init: ");
        goto end;
    }

    if (!(sig->flags & SIG_FLAG_MPM)) {
        printf("sig doesn't have mpm flag set: ");
        goto end;
    }

    if (sig->mpm_content_maxlen != 4) {
        printf("mpm content max len %"PRIu16", expected 4: ", sig->mpm_content_maxlen);
        goto end;
    }

    if (sig->mpm_uricontent_maxlen != 0) {
        printf("mpm uricontent max len %"PRIu16", expected 0: ", sig->mpm_uricontent_maxlen);
        goto end;
    }

    result = 1;
end:
    if (sig != NULL)
        SigFree(sig);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test mpm
 */
int SigParseTestMpm02 (void) {
    int result = 0;
    Signature *sig = NULL;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    sig = SigInit(de_ctx, "alert tcp any any -> any any (msg:\"mpm test\"; content:\"abcd\"; content:\"abcdef\"; sid:1;)");
    if (sig == NULL) {
        printf("sig failed to init: ");
        goto end;
    }

    if (!(sig->flags & SIG_FLAG_MPM)) {
        printf("sig doesn't have mpm flag set: ");
        goto end;
    }

    if (sig->mpm_content_maxlen != 6) {
        printf("mpm content max len %"PRIu16", expected 6: ", sig->mpm_content_maxlen);
        goto end;
    }

    if (sig->mpm_uricontent_maxlen != 0) {
        printf("mpm uricontent max len %"PRIu16", expected 0: ", sig->mpm_uricontent_maxlen);
        goto end;
    }

    result = 1;
end:
    if (sig != NULL)
        SigFree(sig);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test test tls (app layer) rule
 */
static int SigParseTestAppLayerTLS01(void) {
    int result = 0;
    DetectEngineCtx *de_ctx;
    Signature *s=NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;
    de_ctx->flags |= DE_QUIET;

    s = SigInit(de_ctx,"alert tls any any -> any any (msg:\"SigParseTestAppLayerTLS01 \"; sid:410006; rev:1;)");
    if (s == NULL) {
        printf("parsing sig failed: ");
        goto end;
    }

    if (s->alproto == 0) {
        printf("alproto not set: ");
        goto end;
    }

    result = 1;
end:
    if (s != NULL)
        SigFree(s);
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);

    return result;
}

/**
 * \test test tls (app layer) rule
 */
static int SigParseTestAppLayerTLS02(void) {
    int result = 0;
    DetectEngineCtx *de_ctx;
    Signature *s=NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;
    de_ctx->flags |= DE_QUIET;

    s = SigInit(de_ctx,"alert tls any any -> any any (msg:\"SigParseTestAppLayerTLS02 \"; tls.version:1.0; sid:410006; rev:1;)");
    if (s == NULL) {
        printf("parsing sig failed: ");
        goto end;
    }

    if (s->alproto == 0) {
        printf("alproto not set: ");
        goto end;
    }

    result = 1;
end:
    if (s != NULL)
        SigFree(s);
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test test tls (app layer) rule
 */
static int SigParseTestAppLayerTLS03(void) {
    int result = 0;
    DetectEngineCtx *de_ctx;
    Signature *s=NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;
    de_ctx->flags |= DE_QUIET;

    s = SigInit(de_ctx,"alert tls any any -> any any (msg:\"SigParseTestAppLayerTLS03 \"; tls.version:2.5; sid:410006; rev:1;)");
    if (s != NULL) {
        SigFree(s);
        goto end;
    }

    result = 1;
end:
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);
    return result;
}
#endif /* UNITTESTS */

void SigParseRegisterTests(void) {
#ifdef UNITTESTS
    UtRegisterTest("SigParseTest01", SigParseTest01, 1);
    UtRegisterTest("SigParseTest02", SigParseTest02, 1);
    UtRegisterTest("SigParseTest03", SigParseTest03, 1);
    UtRegisterTest("SigParseTest04", SigParseTest04, 1);
    UtRegisterTest("SigParseTest05", SigParseTest05, 1);
    UtRegisterTest("SigParseTest06", SigParseTest06, 1);

    UtRegisterTest("SigParseBidirecTest06", SigParseBidirecTest06, 1);
    UtRegisterTest("SigParseBidirecTest07", SigParseBidirecTest07, 1);
    UtRegisterTest("SigParseBidirecTest08", SigParseBidirecTest08, 1);
    UtRegisterTest("SigParseBidirecTest09", SigParseBidirecTest09, 1);
    UtRegisterTest("SigParseBidirecTest10", SigParseBidirecTest10, 1);
    UtRegisterTest("SigParseBidirecTest11", SigParseBidirecTest11, 1);
    UtRegisterTest("SigParseBidirecTest12", SigParseBidirecTest12, 1);
    UtRegisterTest("SigParseBidirecTest13", SigParseBidirecTest13, 1);
    UtRegisterTest("SigParseBidirecTest14", SigParseBidirecTest14, 1);
    UtRegisterTest("SigTestBidirec01", SigTestBidirec01, 1);
    UtRegisterTest("SigTestBidirec02", SigTestBidirec02, 1);
    UtRegisterTest("SigTestBidirec03", SigTestBidirec03, 1);
    UtRegisterTest("SigTestBidirec04", SigTestBidirec04, 1);
    UtRegisterTest("SigParseTestNegation01", SigParseTestNegation01, 1);
    UtRegisterTest("SigParseTestNegation02", SigParseTestNegation02, 1);
    UtRegisterTest("SigParseTestNegation03", SigParseTestNegation03, 1);
    UtRegisterTest("SigParseTestNegation04", SigParseTestNegation04, 1);
    UtRegisterTest("SigParseTestNegation05", SigParseTestNegation05, 1);
    UtRegisterTest("SigParseTestNegation06", SigParseTestNegation06, 1);
    UtRegisterTest("SigParseTestNegation07", SigParseTestNegation07, 1);
    UtRegisterTest("SigParseTestMpm01", SigParseTestMpm01, 1);
    UtRegisterTest("SigParseTestMpm02", SigParseTestMpm02, 1);
    UtRegisterTest("SigParseTestAppLayerTLS01", SigParseTestAppLayerTLS01, 1);
    UtRegisterTest("SigParseTestAppLayerTLS02", SigParseTestAppLayerTLS02, 1);
    UtRegisterTest("SigParseTestAppLayerTLS03", SigParseTestAppLayerTLS03, 1);
#endif /* UNITTESTS */
}
