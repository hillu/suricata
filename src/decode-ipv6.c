/* Copyright (C) 2007-2010 Victor Julien <victor@inliniac.net>
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
 * Decode IPv6
 */

#include "suricata-common.h"
#include "packet-queue.h"
#include "decode.h"
#include "decode-ipv6.h"
#include "decode-icmpv6.h"
#include "decode-events.h"
#include "defrag.h"
#include "util-debug.h"

#define IPV6_EXTHDRS     ip6eh.ip6_exthdrs
#define IPV6_EH_CNT      ip6eh.ip6_exthdrs_cnt

static void
DecodeIPV6ExtHdrs(ThreadVars *tv, DecodeThreadVars *dtv, Packet *p, uint8_t *pkt, uint16_t len, PacketQueue *pq)
{
    uint8_t *orig_pkt = pkt;
    uint8_t nh;
    uint8_t hdrextlen;
    uint16_t plen;
    char dstopts = 0;
    char exthdr_fh_done = 0;

    nh = IPV6_GET_NH(p);
    plen = len;

    while(1)
    {
        if (plen < 2) /* minimal needed in a hdr */
            return;

        switch(nh)
        {
            case IPPROTO_TCP:
                IPV6_SET_L4PROTO(p,nh);
                DecodeTCP(tv, dtv, p, pkt, plen, pq);
                return;

            case IPPROTO_UDP:
                IPV6_SET_L4PROTO(p,nh);
                DecodeUDP(tv, dtv, p, pkt, plen, pq);
                return;

            case IPPROTO_ICMPV6:
                IPV6_SET_L4PROTO(p,nh);
                DecodeICMPV6(tv, dtv, p, pkt, plen, pq);
                return;

            case IPPROTO_ROUTING:
                hdrextlen = (*(pkt+1) + 1) << 3;  /* 8 octet units */
                if (hdrextlen > plen) {
                    DECODER_SET_EVENT(p, IPV6_TRUNC_EXTHDR);
                    return;
                }

                if (p->IPV6_EH_CNT < IPV6_MAX_OPT)
                {
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].type = nh;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].next = *pkt;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].len = hdrextlen;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].data = pkt+2;
                    p->IPV6_EH_CNT++;
                }

                if (IPV6_EXTHDR_ISSET_RH(p)) {
                    DECODER_SET_EVENT(p, IPV6_EXTHDR_DUPL_RH);
                    /* skip past this extension so we can continue parsing the rest
                     * of the packet */
                    nh = *pkt;
                    pkt += hdrextlen;
                    plen -= hdrextlen;
                    break;
                }

                IPV6_EXTHDR_SET_RH(p, pkt);
                IPV6_EXTHDR_RH(p)->ip6rh_len = hdrextlen;
/** \todo move into own function and load on demand */
                if (IPV6_EXTHDR_RH(p)->ip6rh_type == 0) {
                    uint8_t i;

                    uint8_t n = IPV6_EXTHDR_RH(p)->ip6rh_len / 2;

                    /* because we devide the header len by 2 (as rfc 2460 tells us to)
                     * we devide the result by 8 and not 16 as the header fields are
                     * sized */
                    for (i = 0; i < (n/8) && i < sizeof(IPV6_EXTHDR_RH(p)->ip6rh0_addr)/sizeof(struct in6_addr); ++i) {
                        /* the address header fields are 16 bytes in size */
/** \todo do this without memcpy since it's expensive */
                        memcpy(&IPV6_EXTHDR_RH(p)->ip6rh0_addr[i], pkt+(i*16)+8, sizeof(IPV6_EXTHDR_RH(p)->ip6rh0_addr[i]));
                    }
                    IPV6_EXTHDR_RH(p)->ip6rh0_num_addrs = i;
                }

                nh = *pkt;
                pkt += hdrextlen;
                plen -= hdrextlen;
                break;

            case IPPROTO_HOPOPTS:
            case IPPROTO_DSTOPTS:
            {
                IPV6OptHAO *hao = NULL;
                IPV6OptRA *ra = NULL;
                IPV6OptJumbo *jumbo = NULL;
                uint8_t optslen = 0;

                hdrextlen =  (*(pkt+1) + 1) << 3;
                if (hdrextlen > plen) {
                    DECODER_SET_EVENT(p, IPV6_TRUNC_EXTHDR);
                    return;
                }

                if (p->IPV6_EH_CNT < IPV6_MAX_OPT)
                {
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].type = nh;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].next = *pkt;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].len = hdrextlen;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].data = pkt+2;
                    p->IPV6_EH_CNT++;
                }

                uint8_t *ptr = pkt + 2; /* +2 to go past nxthdr and len */

                /* point the pointers to right structures
                 * in Packet. */
                if (nh == IPPROTO_HOPOPTS) {
                    if (IPV6_EXTHDR_ISSET_HH(p)) {
                        DECODER_SET_EVENT(p, IPV6_EXTHDR_DUPL_HH);
                        /* skip past this extension so we can continue parsing the rest
                         * of the packet */
                        nh = *pkt;
                        pkt += hdrextlen;
                        plen -= hdrextlen;
                        break;
                    }

                    IPV6_EXTHDR_SET_HH(p, pkt);
                    hao = &IPV6_EXTHDR_HH_HAO(p);
                    ra = &IPV6_EXTHDR_HH_RA(p);
                    jumbo = &IPV6_EXTHDR_HH_JUMBO(p);

                    optslen = ((IPV6_EXTHDR_HH(p)->ip6hh_len+1)<<3)-2;
                }
                else if (nh == IPPROTO_DSTOPTS)
                {
                    if (dstopts == 0) {
                        IPV6_EXTHDR_SET_DH1(p, pkt);
                        hao = &IPV6_EXTHDR_DH1_HAO(p);
                        ra = &IPV6_EXTHDR_DH1_RA(p);
                        jumbo = &IPV6_EXTHDR_DH2_JUMBO(p);
                        optslen = ((IPV6_EXTHDR_DH1(p)->ip6dh_len+1)<<3)-2;
                        dstopts = 1;
                    } else if (dstopts == 1) {
                        IPV6_EXTHDR_SET_DH2(p, pkt);
                        hao = &IPV6_EXTHDR_DH2_HAO(p);
                        ra = &IPV6_EXTHDR_DH2_RA(p);
                        jumbo = &IPV6_EXTHDR_DH2_JUMBO(p);
                        optslen = ((IPV6_EXTHDR_DH2(p)->ip6dh_len+1)<<3)-2;
                        dstopts = 2;
                    } else {
                        DECODER_SET_EVENT(p, IPV6_EXTHDR_DUPL_DH);
                        /* skip past this extension so we can continue parsing the rest
                         * of the packet */
                        nh = *pkt;
                        pkt += hdrextlen;
                        plen -= hdrextlen;
                        break;
                    }
                }

                if (optslen > plen) {
                    /* since the packet is long enough (we checked
                     * plen against hdrlen, the optlen must be malformed. */
                    DECODER_SET_EVENT(p, IPV6_EXTHDR_INVALID_OPTLEN);
                    /* skip past this extension so we can continue parsing the rest
                     * of the packet */
                    nh = *pkt;
                    pkt += hdrextlen;
                    plen -= hdrextlen;
                    break;
                }
/** \todo move into own function to loaded on demand */
                uint16_t offset = 0;
                while(offset < optslen)
                {
                    if (*ptr == IPV6OPT_PADN) /* PadN */
                    {
                        //printf("PadN option\n");
                    }
                    else if (*ptr == IPV6OPT_RA) /* RA */
                    {
                        ra->ip6ra_type = *(ptr);
                        ra->ip6ra_len  = *(ptr + 1);
                        memcpy(&ra->ip6ra_value, (ptr + 2), sizeof(ra->ip6ra_value));
                        ra->ip6ra_value = ntohs(ra->ip6ra_value);
                        //printf("RA option: type %" PRIu32 " len %" PRIu32 " value %" PRIu32 "\n",
                        //    ra->ip6ra_type, ra->ip6ra_len, ra->ip6ra_value);
                    }
                    else if (*ptr == IPV6OPT_JUMBO) /* Jumbo */
                    {
                        jumbo->ip6j_type = *(ptr);
                        jumbo->ip6j_len  = *(ptr+1);
                        memcpy(&jumbo->ip6j_payload_len, (ptr+2), sizeof(jumbo->ip6j_payload_len));
                        jumbo->ip6j_payload_len = ntohl(jumbo->ip6j_payload_len);
                        //printf("Jumbo option: type %" PRIu32 " len %" PRIu32 " payload len %" PRIu32 "\n",
                        //    jumbo->ip6j_type, jumbo->ip6j_len, jumbo->ip6j_payload_len);
                    }
                    else if (*ptr == IPV6OPT_HAO) /* HAO */
                    {
                        hao->ip6hao_type = *(ptr);
                        hao->ip6hao_len  = *(ptr+1);
                        memcpy(&hao->ip6hao_hoa, (ptr+2), sizeof(hao->ip6hao_hoa));
                        //printf("HAO option: type %" PRIu32 " len %" PRIu32 " ",
                        //    hao->ip6hao_type, hao->ip6hao_len);
                        //char addr_buf[46];
                        //inet_ntop(AF_INET6, (char *)&(hao->ip6hao_hoa),
                        //    addr_buf,sizeof(addr_buf));
                        //printf("home addr %s\n", addr_buf);
                    }
                    uint16_t len = (*(ptr + 1) + 2);
                    ptr += len; /* +2 for opt type and opt len fields */
                    offset += len;
                }

                nh = *pkt;
                pkt += hdrextlen;
                plen -= hdrextlen;
                break;
            }

            case IPPROTO_FRAGMENT:
                /* store the offset of this extension into the packet
                 * past the ipv6 header. We use it in defrag for creating
                 * a defragmented packet without the frag header */
                if (exthdr_fh_done == 0) {
                    p->ip6eh.fh_offset = pkt - orig_pkt;
                    exthdr_fh_done = 1;
                }

                hdrextlen = sizeof(IPV6FragHdr);
                if (hdrextlen > plen) {
                    DECODER_SET_EVENT(p, IPV6_TRUNC_EXTHDR);
                    return;
                }

                if(p->IPV6_EH_CNT<IPV6_MAX_OPT)
                {
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].type = nh;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].next = *pkt;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].len = hdrextlen;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].data = pkt+2;
                    p->IPV6_EH_CNT++;
                }

                if (IPV6_EXTHDR_ISSET_FH(p)) {
                    DECODER_SET_EVENT(p, IPV6_EXTHDR_DUPL_FH);
                    nh = *pkt;
                    pkt += hdrextlen;
                    plen -= hdrextlen;
                    break;
                }

                /* set the header ptr first */
                IPV6_EXTHDR_SET_FH(p, pkt);

                nh = *pkt;
                pkt += hdrextlen;
                plen -= hdrextlen;
                break;

            case IPPROTO_ESP:
            {
                hdrextlen = sizeof(IPV6EspHdr);
                if (hdrextlen > plen) {
                    DECODER_SET_EVENT(p, IPV6_TRUNC_EXTHDR);
                    return;
                }

                if(p->IPV6_EH_CNT<IPV6_MAX_OPT)
                {
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].type = nh;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].next = IPPROTO_NONE;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].len = hdrextlen;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].data = pkt+2;
                    p->IPV6_EH_CNT++;
                }

                if (IPV6_EXTHDR_ISSET_EH(p)) {
                    DECODER_SET_EVENT(p, IPV6_EXTHDR_DUPL_EH);
                    return;
                }

                IPV6_EXTHDR_SET_EH(p, pkt);

                nh = IPPROTO_NONE;
                pkt += hdrextlen;
                plen -= hdrextlen;
                break;
            }
            case IPPROTO_AH:
            {
                hdrextlen =  (*(pkt+1) + 1) << 3;
                if (hdrextlen > plen) {
                    DECODER_SET_EVENT(p, IPV6_TRUNC_EXTHDR);
                    return;
                }

                if(p->IPV6_EH_CNT<IPV6_MAX_OPT)
                {
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].type = nh;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].next = *pkt;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].len = hdrextlen;
                    p->IPV6_EXTHDRS[p->IPV6_EH_CNT].data = pkt+2;
                    p->IPV6_EH_CNT++;
                }

                if (IPV6_EXTHDR_ISSET_AH(p)) {
                    DECODER_SET_EVENT(p, IPV6_EXTHDR_DUPL_AH);
                    nh = *pkt;
                    pkt += hdrextlen;
                    plen -= hdrextlen;
                    break;
                }

                IPV6_EXTHDR_SET_AH(p, pkt);

                nh = *pkt;
                pkt += hdrextlen;
                plen -= hdrextlen;
                break;
            }
            case IPPROTO_NONE:
                IPV6_SET_L4PROTO(p,nh);
                return;

            default:
                IPV6_SET_L4PROTO(p,nh);
                return;
        }
    }

    return;
}

static int DecodeIPV6Packet (ThreadVars *tv, DecodeThreadVars *dtv, Packet *p, uint8_t *pkt, uint16_t len)
{
    if (len < IPV6_HEADER_LEN) {
        return -1;
    }

    if (IP_GET_RAW_VER(pkt) != 6) {
        SCLogDebug("wrong ip version %" PRIu8 "",IP_GET_RAW_VER(pkt));
        DECODER_SET_EVENT(p,IPV6_WRONG_IP_VER);
        return -1;
    }

    p->ip6h = (IPV6Hdr *)pkt;

    if (len < (IPV6_HEADER_LEN + IPV6_GET_PLEN(p)))
    {
        DECODER_SET_EVENT(p,IPV6_TRUNC_PKT);
        return -1;
    }

    SET_IPV6_SRC_ADDR(p,&p->src);
    SET_IPV6_DST_ADDR(p,&p->dst);

    return 0;
}

void DecodeIPV6(ThreadVars *tv, DecodeThreadVars *dtv, Packet *p, uint8_t *pkt, uint16_t len, PacketQueue *pq)
{
    int ret;

    SCPerfCounterIncr(dtv->counter_ipv6, tv->sc_perf_pca);

    IPV6_CACHE_INIT(p);

    /* do the actual decoding */
    ret = DecodeIPV6Packet (tv, dtv, p, pkt, len);
    if (ret < 0) {
        p->ip6h = NULL;
        return;
    }

#ifdef DEBUG
    /* debug print */
    char s[46], d[46];
    inet_ntop(AF_INET6, (const void *)GET_IPV6_SRC_ADDR(p), s, sizeof(s));
    inet_ntop(AF_INET6, (const void *)GET_IPV6_DST_ADDR(p), d, sizeof(d));
    SCLogDebug("IPV6 %s->%s - CLASS: %" PRIu32 " FLOW: %" PRIu32 " NH: %" PRIu32 " PLEN: %" PRIu32 " HLIM: %" PRIu32 "", s,d,
        IPV6_GET_CLASS(p), IPV6_GET_FLOW(p), IPV6_GET_NH(p), IPV6_GET_PLEN(p),
        IPV6_GET_HLIM(p));
#endif /* DEBUG */

    /* now process the Ext headers and/or the L4 Layer */
    switch(IPV6_GET_NH(p)) {
        case IPPROTO_TCP:
            return DecodeTCP(tv, dtv, p, pkt + IPV6_HEADER_LEN, IPV6_GET_PLEN(p), pq);
        case IPPROTO_UDP:
            return DecodeUDP(tv, dtv, p, pkt + IPV6_HEADER_LEN, IPV6_GET_PLEN(p), pq);
            break;
        case IPPROTO_ICMPV6:
            return DecodeICMPV6(tv, dtv, p, pkt + IPV6_HEADER_LEN, IPV6_GET_PLEN(p), pq);
        case IPPROTO_FRAGMENT:
        case IPPROTO_HOPOPTS:
        case IPPROTO_ROUTING:
        case IPPROTO_NONE:
        case IPPROTO_DSTOPTS:
        case IPPROTO_AH:
        case IPPROTO_ESP:
            DecodeIPV6ExtHdrs(tv, dtv, p, pkt + IPV6_HEADER_LEN, IPV6_GET_PLEN(p), pq);
            break;
    }

    /* Pass to defragger if a fragment. */
    if (IPV6_EXTHDR_ISSET_FH(p)) {
        Packet *rp = Defrag(tv, dtv, NULL, p);
        if (rp != NULL) {
            DecodeIPV6(tv, dtv, rp, rp->pkt, rp->pktlen, pq);
            PacketEnqueue(pq, rp);

            /* Not really a tunnel packet, but we're piggybacking that
             * functionality for now. */
            SET_TUNNEL_PKT(p);
        }
    }

#ifdef DEBUG
    if (IPV6_EXTHDR_ISSET_FH(p)) {
        SCLogDebug("IPV6 FRAG - HDRLEN: %" PRIuMAX " NH: %" PRIu32 " OFFSET: %" PRIu32 " ID: %" PRIu32 "",
            (uintmax_t)IPV6_EXTHDR_GET_FH_HDRLEN(p), IPV6_EXTHDR_GET_FH_NH(p),
            IPV6_EXTHDR_GET_FH_OFFSET(p), IPV6_EXTHDR_GET_FH_ID(p));
    }
    if (IPV6_EXTHDR_ISSET_RH(p)) {
        SCLogDebug("IPV6 ROUTE - HDRLEN: %" PRIu32 " NH: %" PRIu32 " TYPE: %" PRIu32 "",
            IPV6_EXTHDR_GET_RH_HDRLEN(p), IPV6_EXTHDR_GET_RH_NH(p),
            IPV6_EXTHDR_GET_RH_TYPE(p));
    }
    if (IPV6_EXTHDR_ISSET_HH(p)) {
        SCLogDebug("IPV6 HOPOPT - HDRLEN: %" PRIu32 " NH: %" PRIu32 "",
            IPV6_EXTHDR_GET_HH_HDRLEN(p), IPV6_EXTHDR_GET_HH_NH(p));
    }
    if (IPV6_EXTHDR_ISSET_DH1(p)) {
        SCLogDebug("IPV6 DSTOPT1 - HDRLEN: %" PRIu32 " NH: %" PRIu32 "",
            IPV6_EXTHDR_GET_DH1_HDRLEN(p), IPV6_EXTHDR_GET_DH1_NH(p));
    }
    if (IPV6_EXTHDR_ISSET_DH2(p)) {
        SCLogDebug("IPV6 DSTOPT2 - HDRLEN: %" PRIu32 " NH: %" PRIu32 "",
            IPV6_EXTHDR_GET_DH2_HDRLEN(p), IPV6_EXTHDR_GET_DH2_NH(p));
    }
#endif
    return;
}

