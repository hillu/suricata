/* Copyright (C) 2007-2011 Open Information Security Foundation
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
 * \author Endace Technology Limited.
 * \author Victor Julien <victor@inliniac.net>
 */

#ifndef __UTIL_PROFILE_H__
#define __UTIL_PROFILE_H__

#ifdef PROFILING

#include "util-cpu.h"

extern int profiling_rules_enabled;
extern int profiling_packets_enabled;
extern __thread int profiling_rules_entered;

void SCProfilingPrintPacketProfile(Packet *);
void SCProfilingAddPacket(Packet *);

#define RULE_PROFILING_START \
    uint64_t profile_rule_start_ = 0; \
    uint64_t profile_rule_end_ = 0; \
    if (profiling_rules_enabled) { \
        if (profiling_rules_entered > 0) { \
            SCLogError(SC_ERR_FATAL, "Re-entered profiling, exiting."); \
            exit(1); \
        } \
        profiling_rules_entered++; \
        profile_rule_start_ = UtilCpuGetTicks(); \
    }

#define RULE_PROFILING_END(r, m) \
    if (profiling_rules_enabled) { \
        profile_rule_end_ = UtilCpuGetTicks(); \
        SCProfilingUpdateRuleCounter(r->profiling_id, \
            profile_rule_end_ - profile_rule_start_, m); \
        profiling_rules_entered--; \
    }

#define PACKET_PROFILING_START(p)                                   \
    if (profiling_packets_enabled) {                                \
        (p)->profile.ticks_start = UtilCpuGetTicks();               \
    }

#define PACKET_PROFILING_END(p)                                     \
    if (profiling_packets_enabled) {                                \
        (p)->profile.ticks_end = UtilCpuGetTicks();                 \
        SCProfilingAddPacket((p));                                  \
    }

#define PACKET_PROFILING_TMM_START(p, id)                           \
    if (profiling_packets_enabled) {                                \
        if ((id) < TMM_SIZE) {                                      \
            (p)->profile.tmm[(id)].ticks_start = UtilCpuGetTicks(); \
        }                                                           \
    }

#define PACKET_PROFILING_TMM_END(p, id)                             \
    if (profiling_packets_enabled) {                                \
        if ((id) < TMM_SIZE) {                                      \
            (p)->profile.tmm[(id)].ticks_end = UtilCpuGetTicks();   \
        }                                                           \
    }

#define PACKET_PROFILING_RESET(p)                                   \
    if (profiling_packets_enabled) {                                \
        memset(&(p)->profile, 0x00, sizeof(PktProfiling));          \
    }

#define PACKET_PROFILING_APP_START(dp, id)                          \
    if (profiling_packets_enabled) {                                \
        (dp)->ticks_start = UtilCpuGetTicks();                      \
        (dp)->alproto = (id);                                       \
    }

#define PACKET_PROFILING_APP_END(dp, id)                            \
    if (profiling_packets_enabled) {                                \
        BUG_ON((id) != (dp)->alproto);                              \
        (dp)->ticks_end = UtilCpuGetTicks();                        \
        if ((dp)->ticks_start != 0 && (dp)->ticks_start < ((dp)->ticks_end)) {  \
            (dp)->ticks_spent = ((dp)->ticks_end - (dp)->ticks_start);  \
        }                                                           \
    }

#define PACKET_PROFILING_APP_PD_START(dp)                           \
    if (profiling_packets_enabled) {                                \
        (dp)->proto_detect_ticks_start = UtilCpuGetTicks();         \
    }

#define PACKET_PROFILING_APP_PD_END(dp)                             \
    if (profiling_packets_enabled) {                                \
        (dp)->proto_detect_ticks_end = UtilCpuGetTicks();           \
        if ((dp)->proto_detect_ticks_start != 0 && (dp)->proto_detect_ticks_start < ((dp)->proto_detect_ticks_end)) {  \
            (dp)->proto_detect_ticks_spent =                        \
                ((dp)->proto_detect_ticks_end - (dp)->proto_detect_ticks_start);  \
        }                                                           \
    }

#define PACKET_PROFILING_APP_RESET(dp)                              \
    if (profiling_packets_enabled) {                                \
        (dp)->ticks_start = 0;                                      \
        (dp)->ticks_end = 0;                                        \
        (dp)->ticks_spent = 0;                                      \
        (dp)->alproto = 0;                                          \
        (dp)->proto_detect_ticks_start = 0;                         \
        (dp)->proto_detect_ticks_end = 0;                           \
        (dp)->proto_detect_ticks_spent = 0;                         \
    }

#define PACKET_PROFILING_APP_STORE(dp, p)                           \
    if (profiling_packets_enabled) {                                \
        if ((dp)->alproto < ALPROTO_MAX) {                          \
            (p)->profile.app[(dp)->alproto].ticks_spent += (dp)->ticks_spent;   \
            (p)->profile.proto_detect += (dp)->proto_detect_ticks_spent;        \
        }                                                           \
    }

#define PACKET_PROFILING_DETECT_START(p, id)                        \
    if (profiling_packets_enabled) {                                \
        if ((id) < PROF_DETECT_SIZE) {                              \
            (p)->profile.detect[(id)].ticks_start = UtilCpuGetTicks(); \
        }                                                           \
    }

#define PACKET_PROFILING_DETECT_END(p, id)                          \
    if (profiling_packets_enabled) {                                \
        if ((id) < TMM_SIZE) {                                      \
            (p)->profile.detect[(id)].ticks_end = UtilCpuGetTicks();\
            if ((p)->profile.detect[(id)].ticks_start != 0 &&       \
                    (p)->profile.detect[(id)].ticks_start < (p)->profile.detect[(id)].ticks_end) {  \
                (p)->profile.detect[(id)].ticks_spent +=            \
                ((p)->profile.detect[(id)].ticks_end - (p)->profile.detect[(id)].ticks_start);  \
            }                                                       \
        }                                                           \
    }

void SCProfilingInit(void);
void SCProfilingDestroy(void);
void SCProfilingInitRuleCounters(DetectEngineCtx *);
void SCProfilingCounterAddUI64(uint16_t, uint64_t);
void SCProfilingRegisterTests(void);
void SCProfilingDump(void);
void SCProfilingUpdateRuleCounter(uint16_t, uint64_t, int);

#else

#define RULE_PROFILING_START
#define RULE_PROFILING_END(r, m)

#define PACKET_PROFILING_START(p)
#define PACKET_PROFILING_END(p)

#define PACKET_PROFILING_TMM_START(p, id)
#define PACKET_PROFILING_TMM_END(p, id)

#define PACKET_PROFILING_RESET(p)

#define PACKET_PROFILING_APP_START(dp, id)
#define PACKET_PROFILING_APP_END(dp, id)
#define PACKET_PROFILING_APP_RESET(dp)
#define PACKET_PROFILING_APP_STORE(dp, p)

#define PACKET_PROFILING_APP_PD_START(dp)
#define PACKET_PROFILING_APP_PD_END(dp)

#define PACKET_PROFILING_DETECT_START(p, id)
#define PACKET_PROFILING_DETECT_END(p, id)

#endif /* PROFILING */

#endif /* ! __UTIL_PROFILE_H__ */
