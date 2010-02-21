/** Copyright (c) 2009 Open Information Security Foundation
 *
 *  \author Breno Silva <breno.silva@gmail.com>
 */

#ifndef __DETECT_DECODE_EVENT_H__
#define __DETECT_DECODE_EVENT_H__

#include "decode-events.h"

typedef struct DetectDecodeEventData_ {
    uint8_t event;
} DetectDecodeEventData;

/* prototypes */
void DetectDecodeEventRegister (void);

/* supported decoder events */

#ifdef DETECT_EVENTS
struct DetectDecodeEvents_ {
    char *event_name;
    uint8_t code;
} DEvents[] = {
    { "ipv4.pkt_too_small", IPV4_PKT_TOO_SMALL, },
    { "ipv4.hlen_too_small", IPV4_HLEN_TOO_SMALL, },
    { "ipv4.iplen_smaller_than_hlen", IPV4_IPLEN_SMALLER_THAN_HLEN, },
    { "ipv4.trunc_pkt", IPV4_TRUNC_PKT, },
    { "ipv4.opt_invalid", IPV4_OPT_INVALID, },
    { "ipv4.opt_invalid_len", IPV4_OPT_INVALID_LEN, },
    { "ipv4.opt_malformed", IPV4_OPT_MALFORMED, },
    { "ipv4.opt_pad_required", IPV4_OPT_PAD_REQUIRED, },
    { "ipv4.opt_eol_required", IPV4_OPT_EOL_REQUIRED, },
    { "ipv4.opt_duplicate", IPV4_OPT_DUPLICATE, },
    { "ipv4.opt_unknown", IPV4_OPT_UNKNOWN, },
    { "ipv6.pkt_too_small", IPV6_PKT_TOO_SMALL, },
    { "ipv6.trunc_pkt", IPV6_TRUNC_PKT, },
    { "ipv6.trunc_exthdr", IPV6_TRUNC_EXTHDR, },
    { "ipv6.exthdr_dupl_fh", IPV6_EXTHDR_DUPL_FH, },
    { "ipv6.exthdr_dupl_rh", IPV6_EXTHDR_DUPL_RH, },
    { "ipv6.exthdr_dupl_hh", IPV6_EXTHDR_DUPL_HH, },
    { "ipv6.exthdr_dupl_dh", IPV6_EXTHDR_DUPL_DH, },
    { "ipv6.exthdr_dupl_ah", IPV6_EXTHDR_DUPL_AH, },
    { "ipv6.exthdr_dupl_eh", IPV6_EXTHDR_DUPL_EH, },
    { "ipv6.exthdr_invalid_optlen", IPV6_EXTHDR_INVALID_OPTLEN, },
    { "icmpv6.unknown_type", ICMPV6_UNKNOWN_TYPE,},
    { "icmpv6.unknown_code", ICMPV6_UNKNOWN_CODE,},
    { "icmpv6.pkt_too_small", ICMPV6_PKT_TOO_SMALL,},
    { "icmpv6.ipv6_unknown_version", ICMPV6_IPV6_UNKNOWN_VER,},
    { "icmpv6.ipv6_trunc_pkt", ICMPV6_IPV6_TRUNC_PKT,},
    { "tcp.pkt_too_small", TCP_PKT_TOO_SMALL, },
    { "tcp.hlen_too_small", TCP_HLEN_TOO_SMALL, },
    { "tcp.invalid_optlen", TCP_INVALID_OPTLEN, },
    { "tcp.opt_invalid_len", TCP_OPT_INVALID_LEN, },
    { "tcp.opt_duplicate", TCP_OPT_DUPLICATE, },
    { "udp.pkt_too_small", UDP_PKT_TOO_SMALL, },
    { "udp.hlen_too_small", UDP_HLEN_TOO_SMALL, },
    { "udp.hlen_invalid", UDP_HLEN_INVALID, },
    { "sll.pkt_too_small", SLL_PKT_TOO_SMALL, },
    { "ethernet.pkt_too_small", ETHERNET_PKT_TOO_SMALL, },
    { "ppp.pkt_too_small", PPP_PKT_TOO_SMALL, },
    { "ppp.ju_pkt_too_small", PPPVJU_PKT_TOO_SMALL, },
    { "ppp.ip4_pkt_too_small", PPPIPV4_PKT_TOO_SMALL, },
    { "ppp.ip6_pkt_too_small", PPPIPV6_PKT_TOO_SMALL, },
    { "ppp.wrong_type", PPP_WRONG_TYPE, }, /** unknown & invalid protocol */
    { "ppp.unsup_proto", PPP_UNSUP_PROTO, }, /** unsupported but valid protocol */
    { "gre.pkt_too_small", GRE_PKT_TOO_SMALL, },
    { "gre.wrong_version", GRE_WRONG_VERSION, },
    { "gre.version0_recur", GRE_VERSION0_RECUR, },
    { "gre.version0_flags", GRE_VERSION0_FLAGS, },
    { "gre.version0_hdr_too_big", GRE_VERSION0_HDR_TOO_BIG, },
    { "gre.version1_chksum", GRE_VERSION1_CHKSUM, },
    { "gre.version1_route", GRE_VERSION1_ROUTE, },
    { "gre.version1_ssr", GRE_VERSION1_SSR, },
    { "gre.version1_recur", GRE_VERSION1_RECUR, },
    { "gre.version1_flags", GRE_VERSION1_FLAGS, },
    { "gre.version1_no_key", GRE_VERSION1_NO_KEY, },
    { "gre.version1_wrong_protocol", GRE_VERSION1_WRONG_PROTOCOL, },
    { "gre.version1_malformed_sre_hdr", GRE_VERSION1_MALFORMED_SRE_HDR, },
    { "gre.version1_hdr_too_big", GRE_VERSION1_HDR_TOO_BIG, },
    { NULL, 0 },
};
#endif /* DETECT_EVENTS */

#endif /*__DETECT_DECODE_EVENT_H__ */

