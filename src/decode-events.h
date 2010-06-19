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
 */

#ifndef __DECODE_EVENTS_H__
#define __DECODE_EVENTS_H__

enum {
    /* IPV4 EVENTS */
    IPV4_PKT_TOO_SMALL = 1,         /**< ipv4 pkt smaller than minimum header size */
    IPV4_HLEN_TOO_SMALL,            /**< ipv4 header smaller than minimum size */
    IPV4_IPLEN_SMALLER_THAN_HLEN,   /**< ipv4 pkt len smaller than ip header size */
    IPV4_TRUNC_PKT,                 /**< truncated ipv4 packet */

    /* IPV4 OPTIONS */
    IPV4_OPT_INVALID,               /**< invalid ip options */
    IPV4_OPT_INVALID_LEN,           /**< ip options with invalid len */
    IPV4_OPT_MALFORMED,             /**< malformed ip options */
    IPV4_OPT_PAD_REQUIRED,          /**< pad bytes are needed in ip options */
    IPV4_OPT_EOL_REQUIRED,          /**< "end of list" needed in ip options */
    IPV4_OPT_DUPLICATE,             /**< duplicated ip option */
    IPV4_OPT_UNKNOWN,               /**< unknown ip option */
    IPV4_WRONG_IP_VER,              /**< wrong ip version in ip options */

    /* ICMP EVENTS */
    ICMPV4_PKT_TOO_SMALL,           /**< icmpv4 packet smaller than minimum size */
    ICMPV4_UNKNOWN_TYPE,            /**< icmpv4 unknown type */
    ICMPV4_UNKNOWN_CODE,            /**< icmpv4 unknown code */
    ICMPV4_IPV4_TRUNC_PKT,          /**< truncated icmpv4 packet */
    ICMPV4_IPV4_UNKNOWN_VER,        /**< unknown version in icmpv4 packet*/

    /* ICMPv6 EVENTS */
    ICMPV6_UNKNOWN_TYPE,            /**< icmpv6 unknown type */
    ICMPV6_UNKNOWN_CODE,            /**< icmpv6 unknown code */
    ICMPV6_PKT_TOO_SMALL,           /**< icmpv6 smaller than minimum size */
    ICMPV6_IPV6_UNKNOWN_VER,        /**< unknown version in icmpv6 packet */
    ICMPV6_IPV6_TRUNC_PKT,          /**< truncated icmpv6 packet */

    /* IPV6 EVENTS */
    IPV6_PKT_TOO_SMALL,             /**< ipv6 packet smaller than minimum size */
    IPV6_TRUNC_PKT,                 /**< truncated ipv6 packet */
    IPV6_TRUNC_EXTHDR,              /**< truncated ipv6 extension header */
    IPV6_EXTHDR_DUPL_FH,            /**< duplicated "fragment" header in ipv6 extension headers */
    IPV6_EXTHDR_DUPL_RH,            /**< duplicated "routing" header in ipv6 extension headers */
    IPV6_EXTHDR_DUPL_HH,            /**< duplicated "hop-by-hop" header in ipv6 extension headers */
    IPV6_EXTHDR_DUPL_DH,            /**< duplicated "destination" header in ipv6 extension headers */
    IPV6_EXTHDR_DUPL_AH,            /**< duplicated "authentication" header in ipv6 extension headers */
    IPV6_EXTHDR_DUPL_EH,            /**< duplicated "ESP" header in ipv6 extension headers */

    IPV6_EXTHDR_INVALID_OPTLEN,     /**< the opt len in an hop or dst hdr is invalid. */
    IPV6_WRONG_IP_VER,              /**< wrong version in ipv6 */

    /* TCP EVENTS */
    TCP_PKT_TOO_SMALL,              /**< tcp packet smaller than minimum size */
    TCP_HLEN_TOO_SMALL,             /**< tcp header smaller than minimum size */
    TCP_INVALID_OPTLEN,             /**< invalid len in tcp options */

    /* TCP OPTIONS */
    TCP_OPT_INVALID_LEN,            /**< tcp option with invalid len */
    TCP_OPT_DUPLICATE,              /**< duplicated tcp option */

    /* UDP EVENTS */
    UDP_PKT_TOO_SMALL,              /**< udp packet smaller than minimum size */
    UDP_HLEN_TOO_SMALL,             /**< udp header smaller than minimum size */
    UDP_HLEN_INVALID,               /**< invalid len of upd header */

    /* SLL EVENTS */
    SLL_PKT_TOO_SMALL,              /**< sll packet smaller than minimum size */

    /* ETHERNET EVENTS */
    ETHERNET_PKT_TOO_SMALL,         /**< ethernet packet smaller than minimum size */

    /* PPP EVENTS */
    PPP_PKT_TOO_SMALL,              /**< ppp packet smaller than minimum size */
    PPPVJU_PKT_TOO_SMALL,           /**< ppp vj uncompressed packet smaller than minimum size */
    PPPIPV4_PKT_TOO_SMALL,          /**< ppp ipv4 packet smaller than minimum size */
    PPPIPV6_PKT_TOO_SMALL,          /**< ppp ipv6 packet smaller than minimum size */
    PPP_WRONG_TYPE,                 /**< wrong type in ppp frame */
    PPP_UNSUP_PROTO,                /**< protocol not supported for ppp */

    /* PPPOE EVENTS */
    PPPOE_PKT_TOO_SMALL,            /**< pppoe packet smaller than minimum size */
    PPPOE_WRONG_CODE,               /**< wrong code for pppoe */
    PPPOE_MALFORMED_TAGS,           /**< malformed tags in pppoe */

    /* GRE EVENTS */
    GRE_PKT_TOO_SMALL,              /**< gre packet smaller than minimum size */
    GRE_WRONG_VERSION,              /**< wrong version in gre header */
    GRE_VERSION0_RECUR,             /**< gre v0 recursion control */
    GRE_VERSION0_FLAGS,             /**< gre v0 flags */
    GRE_VERSION0_HDR_TOO_BIG,       /**< gre v0 header bigger than maximum size */
    GRE_VERSION1_CHKSUM,            /**< gre v1 checksum */
    GRE_VERSION1_ROUTE,             /**< gre v1 routing */
    GRE_VERSION1_SSR,               /**< gre v1 strict source route */
    GRE_VERSION1_RECUR,             /**< gre v1 recursion control */
    GRE_VERSION1_FLAGS,             /**< gre v1 flags */
    GRE_VERSION1_NO_KEY,            /**< gre v1 no key present in header */
    GRE_VERSION1_WRONG_PROTOCOL,    /**< gre v1 wrong protocol */
    GRE_VERSION1_MALFORMED_SRE_HDR, /**< gre v1 malformed source route entry header */
    GRE_VERSION1_HDR_TOO_BIG,       /**< gre v1 header too big */

    /* VLAN EVENTS */
    VLAN_HEADER_TOO_SMALL,          /**< vlan header smaller than minimum size */
    VLAN_UNKNOWN_TYPE,              /**< vlan unknown type */

    /* RAW EVENTS */
    IPRAW_INVALID_IPV,              /**< invalid ip version in ip raw */

    /* should always be last! */
    DECODE_EVENT_MAX,
};

#endif /* __DECODE_EVENTS_H__ */

