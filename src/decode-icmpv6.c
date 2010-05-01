/* Copyright (c) 2008 Victor Julien <victor@inliniac.net> */

#include "suricata-common.h"
#include "decode-icmpv6.h"
#include "decode.h"
#include "decode-tcp.h"
#include "decode-udp.h"
#include "decode-events.h"
#include "util-unittest.h"
#include "flow.h"
#include "util-debug.h"

/**
 * \brief Calculates the checksum for the ICMPV6 packet
 *
 * \param shdr Pointer to source address field from the IPV6 packet.  Used as a
 *             part of the psuedoheader for computing the checksum
 * \param pkt  Pointer to the start of the ICMPV6 packet
 * \param tlen Total length of the ICMPV6 packet(header + payload)
 *
 * \retval csum Checksum for the ICMPV6 packet
 */
inline uint16_t ICMPV6CalculateChecksum(uint16_t *shdr, uint16_t *pkt,
                                        uint16_t tlen)
{
    uint16_t pad = 0;
    uint32_t csum = shdr[0];

    csum += shdr[1] + shdr[2] + shdr[3] + shdr[4] + shdr[5] + shdr[6] +
        shdr[7] + shdr[8] + shdr[9] + shdr[10] + shdr[11] + shdr[12] +
        shdr[13] + shdr[14] + shdr[15] + htons(58 + tlen);

    csum += pkt[0];

    tlen -= 4;
    pkt += 2;

    while (tlen >= 64) {
        csum += pkt[0] + pkt[1] + pkt[2] + pkt[3] + pkt[4] + pkt[5] + pkt[6] +
            pkt[7] + pkt[8] + pkt[9] + pkt[10] + pkt[11] + pkt[12] + pkt[13] +
            pkt[14] + pkt[15] + pkt[16] + pkt[17] + pkt[18] + pkt[19] +
            pkt[20] + pkt[21] + pkt[22] + pkt[23] + pkt[24] + pkt[25] +
            pkt[26] + pkt[27] + pkt[28] + pkt[29] + pkt[30] + pkt[31];
        tlen -= 64;
        pkt += 32;
    }

    while (tlen >= 32) {
        csum += pkt[0] + pkt[1] + pkt[2] + pkt[3] + pkt[4] + pkt[5] + pkt[6] +
            pkt[7] + pkt[8] + pkt[9] + pkt[10] + pkt[11] + pkt[12] + pkt[13] +
            pkt[14] + pkt[15];
        tlen -= 32;
        pkt += 16;
    }

    while(tlen >= 8) {
        csum += pkt[0] + pkt[1] + pkt[2] + pkt[3];
        tlen -= 8;
        pkt += 4;
    }

    while(tlen >= 4) {
        csum += pkt[0] + pkt[1];
        tlen -= 4;
        pkt += 2;
    }

    while (tlen > 1) {
        csum += pkt[0];
        tlen -= 2;
        pkt += 1;
    }

    if (tlen == 1) {
        *(uint8_t *)(&pad) = (*(uint8_t *)pkt);
        csum += pad;
    }

    csum = (csum >> 16) + (csum & 0x0000FFFF);

    return (uint16_t) ~csum;
}


/**
 * \brief Get variables and do some checks of the embedded IPV6 packet
 *
 * \param p Pointer to the packet we are filling
 * \param partial_packet  Pointer to the raw packet buffer
 * \param len the len of the rest of the packet not processed yet
 *
 * \retval void No return value
 */
void DecodePartialIPV6(Packet *p, uint8_t *partial_packet, uint16_t len )
{
    /** Check the sizes, the header must fit at least */
    if (len < IPV6_HEADER_LEN) {
        SCLogDebug("ICMPV6_IPV6_TRUNC_PKT");
        DECODER_SET_EVENT(p, ICMPV6_IPV6_TRUNC_PKT);
        return;
    }

    IPV6Hdr *icmp6_ip6h = (IPV6Hdr*)partial_packet;

    /** Check the embedded version */
    if(((icmp6_ip6h->s_ip6_vfc & 0xf0) >> 4) != 6)
    {
        SCLogDebug("ICMPv6 contains Unknown IPV6 version "
                "ICMPV6_IPV6_UNKNOWN_VER");
        DECODER_SET_EVENT(p, ICMPV6_IPV6_UNKNOWN_VER);
        return;
    }

    /** We need to fill icmpv6vars */
    p->icmpv6vars.emb_ipv6h = icmp6_ip6h;

    /** Get the IP6 address */
    p->icmpv6vars.emb_ip6_src[0] = icmp6_ip6h->ip6_src[0];
    p->icmpv6vars.emb_ip6_src[1] = icmp6_ip6h->ip6_src[1];
    p->icmpv6vars.emb_ip6_src[2] = icmp6_ip6h->ip6_src[2];
    p->icmpv6vars.emb_ip6_src[3] = icmp6_ip6h->ip6_src[3];

    p->icmpv6vars.emb_ip6_dst[0] = icmp6_ip6h->ip6_dst[0];
    p->icmpv6vars.emb_ip6_dst[1] = icmp6_ip6h->ip6_dst[1];
    p->icmpv6vars.emb_ip6_dst[2] = icmp6_ip6h->ip6_dst[2];
    p->icmpv6vars.emb_ip6_dst[3] = icmp6_ip6h->ip6_dst[3];

    /** Get protocol and ports inside the embedded ipv6 packet and set the pointers */
    p->icmpv6vars.emb_ip6_proto_next = icmp6_ip6h->s_ip6_nxt;

    switch (icmp6_ip6h->s_ip6_nxt) {
        case IPPROTO_TCP:
            if (len >= IPV6_HEADER_LEN + TCP_HEADER_LEN ) {
                p->icmpv6vars.emb_tcph = (TCPHdr*)(partial_packet + IPV6_HEADER_LEN);
                p->icmpv6vars.emb_sport = p->icmpv6vars.emb_tcph->th_sport;
                p->icmpv6vars.emb_dport = p->icmpv6vars.emb_tcph->th_dport;

                SCLogDebug("ICMPV6->IPV6->TCP header sport: "
                           "%"PRIu8" dport %"PRIu8"", p->icmpv6vars.emb_sport,
                            p->icmpv6vars.emb_dport);
            } else {
                SCLogDebug("Warning, ICMPV6->IPV6->TCP "
                           "header Didn't fit in the packet!");
                p->icmpv6vars.emb_sport = 0;
                p->icmpv6vars.emb_dport = 0;
            }

            break;
        case IPPROTO_UDP:
            if (len >= IPV6_HEADER_LEN + UDP_HEADER_LEN ) {
                p->icmpv6vars.emb_udph = (UDPHdr*)(partial_packet + IPV6_HEADER_LEN);
                p->icmpv6vars.emb_sport = p->icmpv6vars.emb_udph->uh_sport;
                p->icmpv6vars.emb_dport = p->icmpv6vars.emb_udph->uh_dport;

                SCLogDebug("ICMPV6->IPV6->UDP header sport: "
                           "%"PRIu8" dport %"PRIu8"", p->icmpv6vars.emb_sport,
                            p->icmpv6vars.emb_dport);
            } else {
                SCLogDebug("Warning, ICMPV6->IPV6->UDP "
                           "header Didn't fit in the packet!");
                p->icmpv6vars.emb_sport = 0;
                p->icmpv6vars.emb_dport = 0;
            }

            break;
        case IPPROTO_ICMPV6:
            p->icmpv6vars.emb_icmpv6h = (ICMPV6Hdr*)(partial_packet + IPV6_HEADER_LEN);
            p->icmpv6vars.emb_sport = 0;
            p->icmpv6vars.emb_dport = 0;

            SCLogDebug("ICMPV6->IPV6->ICMP header");

            break;
    }

    /* debug print */
#ifdef DEBUG
    char s[46], d[46];
    inet_ntop(AF_INET6, (const void *)p->icmpv6vars.emb_ip6_src, s, sizeof(s));
    inet_ntop(AF_INET6, (const void *)p->icmpv6vars.emb_ip6_dst, d, sizeof(d));
    SCLogDebug("ICMPv6 embedding IPV6 %s->%s - CLASS: %" PRIu32 " FLOW: "
               "%" PRIu32 " NH: %" PRIu32 " PLEN: %" PRIu32 " HLIM: %" PRIu32,
               s, d, IPV6_GET_RAW_CLASS(icmp6_ip6h), IPV6_GET_RAW_FLOW(icmp6_ip6h),
               IPV6_GET_RAW_NH(icmp6_ip6h), IPV6_GET_RAW_PLEN(icmp6_ip6h), IPV6_GET_RAW_HLIM(icmp6_ip6h));
#endif

    return;
}

/**
 * \brief Decode ICMPV6 packets and fill the Packet with the decoded info
 *
 * \param tv Pointer to the thread variables
 * \param dtv Pointer to the decode thread variables
 * \param p Pointer to the packet we are filling
 * \param pkt Pointer to the raw packet buffer
 * \param len the len of the rest of the packet not processed yet
 * \param pq the packet queue were this packet go
 *
 * \retval void No return value
 */
void DecodeICMPV6(ThreadVars *tv, DecodeThreadVars *dtv, Packet *p,
                  uint8_t *pkt, uint16_t len, PacketQueue *pq)
{
    SCPerfCounterIncr(dtv->counter_icmpv6, tv->sc_perf_pca);

    if (len < ICMPV6_HEADER_LEN) {
        SCLogDebug("ICMPV6_PKT_TOO_SMALL");
        DECODER_SET_EVENT(p, ICMPV6_PKT_TOO_SMALL);
        return;
    }

    p->icmpv6h = (ICMPV6Hdr *)pkt;
    p->proto = IPPROTO_ICMPV6;
    p->type = p->icmpv6h->type;
    p->code = p->icmpv6h->code;

    SCLogDebug("ICMPV6 TYPE %" PRIu32 " CODE %" PRIu32 "", p->icmpv6h->type,
               p->icmpv6h->code);

    switch (ICMPV6_GET_TYPE(p)) {
        case ICMP6_DST_UNREACH:
            SCLogDebug("ICMP6_DST_UNREACH");

            if (ICMPV6_GET_CODE(p) > ICMP6_DST_UNREACH_REJECTROUTE) {
                DECODER_SET_EVENT(p, ICMPV6_UNKNOWN_CODE);
            } else {
                p->icmpv6vars.type = ICMPV6_GET_TYPE(p);
                p->icmpv6vars.code = ICMPV6_GET_CODE(p);
                DecodePartialIPV6(p, (uint8_t*) (pkt + ICMPV6_HEADER_LEN),
                                  len - ICMPV6_HEADER_LEN );
            }

            break;
        case ICMP6_PACKET_TOO_BIG:
            SCLogDebug("ICMP6_PACKET_TOO_BIG");

            if (ICMPV6_GET_CODE(p) != 0) {
                DECODER_SET_EVENT(p, ICMPV6_UNKNOWN_CODE);
            } else {
                p->icmpv6vars.type = ICMPV6_GET_TYPE(p);
                p->icmpv6vars.code = ICMPV6_GET_CODE(p);
                p->icmpv6vars.mtu = ICMPV6_GET_MTU(p);
                DecodePartialIPV6(p, (uint8_t*) (pkt + ICMPV6_HEADER_LEN),
                                  len - ICMPV6_HEADER_LEN );
            }

            break;
        case ICMP6_TIME_EXCEEDED:
            SCLogDebug("ICMP6_TIME_EXCEEDED");

            if (ICMPV6_GET_CODE(p) > ICMP6_TIME_EXCEED_REASSEMBLY) {
                DECODER_SET_EVENT(p, ICMPV6_UNKNOWN_CODE);
            } else {
                p->icmpv6vars.type = ICMPV6_GET_TYPE(p);
                p->icmpv6vars.code= ICMPV6_GET_CODE(p);
                DecodePartialIPV6(p, (uint8_t*) (pkt + ICMPV6_HEADER_LEN),
                                  len - ICMPV6_HEADER_LEN );
            }

            break;
        case ICMP6_PARAM_PROB:
            SCLogDebug("ICMP6_PARAM_PROB");

            if (ICMPV6_GET_CODE(p) > ICMP6_PARAMPROB_OPTION) {
                DECODER_SET_EVENT(p, ICMPV6_UNKNOWN_CODE);
            } else {
                p->icmpv6vars.type = ICMPV6_GET_TYPE(p);
                p->icmpv6vars.code= ICMPV6_GET_CODE(p);
                p->icmpv6vars.error_ptr= ICMPV6_GET_ERROR_PTR(p);
                DecodePartialIPV6(p, (uint8_t*) (pkt + ICMPV6_HEADER_LEN),
                                  len - ICMPV6_HEADER_LEN );
            }

            break;
        case ICMP6_ECHO_REQUEST:
            SCLogDebug("ICMP6_ECHO_REQUEST id: %u seq: %u",
                       p->icmpv6h->icmpv6b.icmpv6i.id, p->icmpv6h->icmpv6b.icmpv6i.seq);

            if (ICMPV6_GET_CODE(p) != 0) {
                DECODER_SET_EVENT(p, ICMPV6_UNKNOWN_CODE);
            } else {
                p->icmpv6vars.type = ICMPV6_GET_TYPE(p);
                p->icmpv6vars.code= ICMPV6_GET_CODE(p);
                p->icmpv6vars.id = p->icmpv6h->icmpv6b.icmpv6i.id;
                p->icmpv6vars.seq = p->icmpv6h->icmpv6b.icmpv6i.seq;
            }

            break;
        case ICMP6_ECHO_REPLY:
            SCLogDebug("ICMP6_ECHO_REPLY id: %u seq: %u",
                       p->icmpv6h->icmpv6b.icmpv6i.id, p->icmpv6h->icmpv6b.icmpv6i.seq);

            if (p->icmpv6h->code != 0) {
                DECODER_SET_EVENT(p, ICMPV6_UNKNOWN_CODE);
            } else {
                p->icmpv6vars.type = ICMPV6_GET_TYPE(p);
                p->icmpv6vars.code= ICMPV6_GET_CODE(p);
                p->icmpv6vars.id = p->icmpv6h->icmpv6b.icmpv6i.id;
                p->icmpv6vars.seq = p->icmpv6h->icmpv6b.icmpv6i.seq;
            }

            break;
        default:
            SCLogDebug("ICMPV6 Message type %" PRIu8 " not "
                       "implemented yet", ICMPV6_GET_TYPE(p));
            DECODER_SET_EVENT(p, ICMPV6_UNKNOWN_TYPE);
    }


    if (DECODER_ISSET_EVENT(p, ICMPV6_UNKNOWN_CODE))
        SCLogDebug("Unknown Code, ICMPV6_UNKNOWN_CODE");

    if (DECODER_ISSET_EVENT(p, ICMPV6_UNKNOWN_TYPE))
        SCLogDebug("Unknown Type, ICMPV6_UNKNOWN_TYPE");

    /* Flow is an integral part of us */
    FlowHandlePacket(tv, p);

    return;
}

#ifdef UNITTESTS

static int ICMPV6CalculateValidChecksumtest01(void) {
    uint16_t csum = 0;

    uint8_t raw_ipv6[] = {
        0x00, 0x00, 0x86, 0x05, 0x80, 0xda, 0x00, 0x60,
        0x97, 0x07, 0x69, 0xea, 0x86, 0xdd, 0x60, 0x00,
        0x00, 0x00, 0x00, 0x44, 0x3a, 0x40, 0x3f, 0xfe,
        0x05, 0x07, 0x00, 0x00, 0x00, 0x01, 0x02, 0x60,
        0x97, 0xff, 0xfe, 0x07, 0x69, 0xea, 0x3f, 0xfe,
        0x05, 0x07, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00,
        0x86, 0xff, 0xfe, 0x05, 0x80, 0xda, 0x03, 0x00,
        0xf7, 0x52, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00,
        0x00, 0x00, 0x00, 0x14, 0x11, 0x01, 0x3f, 0xfe,
        0x05, 0x07, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00,
        0x86, 0xff, 0xfe, 0x05, 0x80, 0xda, 0x3f, 0xfe,
        0x05, 0x01, 0x04, 0x10, 0x00, 0x00, 0x02, 0xc0,
        0xdf, 0xff, 0xfe, 0x47, 0x03, 0x3e, 0xa0, 0x75,
        0x82, 0x9b, 0x00, 0x14, 0x82, 0x8b, 0x01, 0x01,
        0x00, 0x00, 0xf9, 0xc8, 0xe7, 0x36, 0xf5, 0xed,
        0x08, 0x00};

    csum = *( ((uint16_t *)(raw_ipv6 + 56)));

    return (csum == ICMPV6CalculateChecksum((uint16_t *)(raw_ipv6 + 14 + 8),
                                            (uint16_t *)(raw_ipv6 + 54), 68));
}

static int ICMPV6CalculateInvalidChecksumtest02(void) {
    uint16_t csum = 0;

    uint8_t raw_ipv6[] = {
        0x00, 0x00, 0x86, 0x05, 0x80, 0xda, 0x00, 0x60,
        0x97, 0x07, 0x69, 0xea, 0x86, 0xdd, 0x60, 0x00,
        0x00, 0x00, 0x00, 0x44, 0x3a, 0x40, 0x3f, 0xfe,
        0x05, 0x07, 0x00, 0x00, 0x00, 0x01, 0x02, 0x60,
        0x97, 0xff, 0xfe, 0x07, 0x69, 0xea, 0x3f, 0xfe,
        0x05, 0x07, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00,
        0x86, 0xff, 0xfe, 0x05, 0x80, 0xda, 0x03, 0x00,
        0xf7, 0x52, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00,
        0x00, 0x00, 0x00, 0x14, 0x11, 0x01, 0x3f, 0xfe,
        0x05, 0x07, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00,
        0x86, 0xff, 0xfe, 0x05, 0x80, 0xda, 0x3f, 0xfe,
        0x05, 0x01, 0x04, 0x10, 0x00, 0x00, 0x02, 0xc0,
        0xdf, 0xff, 0xfe, 0x47, 0x03, 0x3e, 0xa0, 0x75,
        0x82, 0x9b, 0x00, 0x14, 0x82, 0x8b, 0x01, 0x01,
        0x00, 0x00, 0xf9, 0xc8, 0xe7, 0x36, 0xf5, 0xed,
        0x08, 0x01};

    csum = *( ((uint16_t *)(raw_ipv6 + 56)));

    return (csum == ICMPV6CalculateChecksum((uint16_t *)(raw_ipv6 + 14 + 8),
                                            (uint16_t *)(raw_ipv6 + 54), 68));
}


/** \test icmpv6 message type: parameter problem, valid packet
 *
 * \retval retval 0 = Error ; 1 = ok
 */
static int ICMPV6ParamProbTest01(void)
{
    int retval = 0;
    static uint8_t raw_ipv6[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x38, 0x3a, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x04, 0x00, 0xcc, 0x2a, 0x6d, 0x93, 0x0b, 0xdf,
        0x69, 0x70, 0x12, 0xb7, 0x00, 0x08, 0x3a, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x80, 0x00, 0x08, 0xb5, 0x99, 0xc3, 0xde, 0x40 };

    Packet p;
    IPV6Hdr ip6h;
    ThreadVars tv;
    DecodeThreadVars dtv;
    uint32_t *ipv6src;
    uint32_t *ipv6dst;
    ipv6src = (uint32_t*) &raw_ipv6[8];
    ipv6dst = (uint32_t*) &raw_ipv6[24];

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));
    memset(&ip6h, 0, sizeof(IPV6Hdr));

    FlowInitConfig(FLOW_QUIET);
    DecodeIPV6(&tv, &dtv, &p, raw_ipv6, sizeof(raw_ipv6), NULL);
    FlowShutdown();

    if (p.icmpv6h == NULL) {
        SCLogDebug("ICMPv6 Unable to detect icmpv6 layer from ipv6");
        retval = 0;
        goto end;
    }

    if (ICMPV6_GET_TYPE(&p) != 4 || ICMPV6_GET_CODE(&p) != 0 ||
        ICMPV6_GET_EMB_PROTO(&p) != IPPROTO_ICMPV6) {
        SCLogDebug("ICMPv6 not processed at all");
        retval = 0;
        goto end;
    }

    /* Let's check if we retrieved the embedded ipv6 addresses correctly */
    uint32_t i=0;
    for (i = 0; i < 4; i++) {
        if (p.icmpv6vars.emb_ip6_src[i] != ipv6src[i] ||
            p.icmpv6vars.emb_ip6_dst[i] != ipv6dst[i]) {
            SCLogDebug("ICMPv6 DecodePartialICMPV6 (Embedded ip6h) didn't set "
                       "the src and dest ip addresses correctly");
            retval = 0;
            goto end;
        }
    }

    retval = 1;
end:
    return retval;
}

/** \test icmpv6 message type: packet too big, valid packet
 *
 * \retval retval 0 = Error ; 1 = ok
 */
static int ICMPV6PktTooBigTest01(void)
{
    int retval = 0;
    static uint8_t raw_ipv6[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x30, 0x3a, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x02, 0x00, 0x5c, 0x7a, 0x00, 0x00, 0x05, 0x00,
        0x64, 0x14, 0xfd, 0xff, 0x00, 0x00, 0x3b, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };

    Packet p;
    IPV6Hdr ip6h;
    ThreadVars tv;
    DecodeThreadVars dtv;
    uint32_t *ipv6src;
    uint32_t *ipv6dst;
    ipv6src = (uint32_t*) &raw_ipv6[8];
    ipv6dst = (uint32_t*) &raw_ipv6[24];

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));
    memset(&ip6h, 0, sizeof(IPV6Hdr));

    FlowInitConfig(FLOW_QUIET);
    DecodeIPV6(&tv, &dtv, &p, raw_ipv6, sizeof(raw_ipv6), NULL);
    FlowShutdown();

    if (p.icmpv6h == NULL) {
        SCLogDebug("ICMPv6 Unable to detect icmpv6 layer from ipv6");
        retval = 0;
        goto end;
    }

    /* Note: it has an embedded ipv6 packet but no protocol after ipv6 (IPPROTO_NONE) */
    if (ICMPV6_GET_TYPE(&p) != 2 || ICMPV6_GET_CODE(&p) != 0 ) {
        SCLogDebug("ICMPv6 Not processed at all");
        retval = 0;
        goto end;
    }

    /* Let's check if we retrieved the embedded ipv6 addresses correctly */
    uint32_t i=0;
    for (i = 0; i < 4; i++) {
        if (p.icmpv6vars.emb_ip6_src[i] != ipv6src[i] ||
            p.icmpv6vars.emb_ip6_dst[i] != ipv6dst[i]) {
            SCLogDebug("ICMPv6 DecodePartialICMPV6 (Embedded ip6h) didn't set "
                       "the src and dest ip addresses correctly");
            retval = 0;
            goto end;
        }
    }

    SCLogDebug("ICMPV6 IPV6 src and dst properly set");

    retval = 1;
end:
    return retval;
}

/** \test icmpv6 message type: time exceed, valid packet
 *
 * \retval retval 0 = Error ; 1 = ok
 */
static int ICMPV6TimeExceedTest01(void)
{
    int retval = 0;
    static uint8_t raw_ipv6[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x30, 0x3a, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x03, 0x00, 0x56, 0x2d, 0x00, 0x00, 0x00, 0x00,
        0x6d, 0x23, 0xff, 0x3d, 0x00, 0x00, 0x3b, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };

    Packet p;
    IPV6Hdr ip6h;
    ThreadVars tv;
    DecodeThreadVars dtv;
    uint32_t *ipv6src;
    uint32_t *ipv6dst;
    ipv6src = (uint32_t*) &raw_ipv6[8];
    ipv6dst = (uint32_t*) &raw_ipv6[24];


    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));
    memset(&ip6h, 0, sizeof(IPV6Hdr));

    FlowInitConfig(FLOW_QUIET);
    DecodeIPV6(&tv, &dtv, &p, raw_ipv6, sizeof(raw_ipv6), NULL);
    FlowShutdown();

    if (p.icmpv6h == NULL) {
        SCLogDebug("ICMPv6 Unable to detect icmpv6 layer from ipv6");
        retval = 0;
        goto end;
    }

    /* Note: it has an embedded ipv6 packet but no protocol after ipv6 (IPPROTO_NONE) */
    if (ICMPV6_GET_TYPE(&p) != 3 || ICMPV6_GET_CODE(&p) != 0 ||
        ICMPV6_GET_EMB_IPV6(&p)==NULL || ICMPV6_GET_EMB_PROTO(&p) != IPPROTO_NONE ) {
        SCLogDebug("ICMPv6 Not processed at all");
        retval = 0;
        goto end;
    }

    /* Let's check if we retrieved the embedded ipv6 addresses correctly */
    uint32_t i=0;
    for (i = 0; i < 4; i++) {
        if (p.icmpv6vars.emb_ip6_src[i] != ipv6src[i] ||
            p.icmpv6vars.emb_ip6_dst[i] != ipv6dst[i]) {
            SCLogDebug("ICMPv6 DecodePartialICMPV6 (Embedded ip6h) didn't set "
                       "the src and dest ip addresses correctly");
            retval = 0;
            goto end;
        }
    }

    SCLogDebug("ICMPV6 IPV6 src and dst properly set");

    retval = 1;
end:
    return retval;
}

/** \test icmpv6 message type: destination unreach, valid packet
 *
 * \retval retval 0 = Error ; 1 = ok
 */
static int ICMPV6DestUnreachTest01(void)
{
    int retval = 0;
    static uint8_t raw_ipv6[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x30, 0x3a, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x01, 0x00, 0x7b, 0x85, 0x00, 0x00, 0x00, 0x00,
        0x60, 0x4b, 0xe8, 0xbd, 0x00, 0x00, 0x3b, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };

    Packet p;
    IPV6Hdr ip6h;
    ThreadVars tv;
    DecodeThreadVars dtv;
    uint32_t *ipv6src;
    uint32_t *ipv6dst;
    ipv6src = (uint32_t*) &raw_ipv6[8];
    ipv6dst = (uint32_t*) &raw_ipv6[24];


    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));
    memset(&ip6h, 0, sizeof(IPV6Hdr));

    FlowInitConfig(FLOW_QUIET);
    DecodeIPV6(&tv, &dtv, &p, raw_ipv6, sizeof(raw_ipv6), NULL);
    FlowShutdown();

    if (p.icmpv6h == NULL) {
        SCLogDebug("ICMPv6 Unable to detect icmpv6 layer from ipv6");
        retval = 0;
        goto end;
    }

    /* Note: it has an embedded ipv6 packet but no protocol after ipv6 (IPPROTO_NONE) */
    if (ICMPV6_GET_TYPE(&p) != 1 || ICMPV6_GET_CODE(&p) != 0 ||
        ICMPV6_GET_EMB_IPV6(&p) == NULL || ICMPV6_GET_EMB_PROTO(&p) != IPPROTO_NONE ) {
        SCLogDebug("ICMPv6 Not processed at all");
        retval = 0;
        goto end;
    }

    /* Let's check if we retrieved the embedded ipv6 addresses correctly */
    uint32_t i=0;
    for (i = 0; i < 4; i++) {
        if (p.icmpv6vars.emb_ip6_src[i] != ipv6src[i] ||
            p.icmpv6vars.emb_ip6_dst[i] != ipv6dst[i]) {
            SCLogDebug("ICMPv6 DecodePartialICMPV6 (Embedded ip6h) didn't set "
                       "the src and dest ip addresses correctly");
            retval = 0;
            goto end;
        }
    }

    retval = 1;
end:
    return retval;
}

/**\test icmpv6 message type: echo request, valid packet
 * \retval retval 0 = Error ; 1 = ok
 */
static int ICMPV6EchoReqTest01(void)
{
    int retval = 0;
    static uint8_t raw_ipv6[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x08, 0x3a, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x80, 0x00, 0xe5, 0xa5, 0x25, 0xf0, 0x75, 0x23 };

    Packet p;
    IPV6Hdr ip6h;
    ThreadVars tv;
    DecodeThreadVars dtv;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));
    memset(&ip6h, 0, sizeof(IPV6Hdr));

    FlowInitConfig(FLOW_QUIET);
    DecodeIPV6(&tv, &dtv, &p, raw_ipv6, sizeof(raw_ipv6), NULL);
    FlowShutdown();

    if (p.icmpv6h == NULL) {
        SCLogDebug("ICMPv6 Unable to detect icmpv6 layer from ipv6");
        retval = 0;
        goto end;
    }

    SCLogDebug("ID: %u seq: %u", ICMPV6_GET_ID(&p), ICMPV6_GET_SEQ(&p));

    if (ICMPV6_GET_TYPE(&p) != 128 || ICMPV6_GET_CODE(&p) != 0 ||
        ICMPV6_GET_ID(&p) != 61477 || ICMPV6_GET_SEQ(&p) != 29987) {
        SCLogDebug("ICMPv6 Echo request decode failed");
        retval = 0;
        goto end;
    }

    retval = 1;
end:
    return retval;
}

/**\test icmpv6 message type: echo reply, valid packet
 * \retval retval 0 = Error ; 1 = ok
 */
static int ICMPV6EchoRepTest01(void)
{
    int retval = 0;
    static uint8_t raw_ipv6[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x08, 0x3a,
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0xff, 0x02, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x81, 0x00,
        0xe5, 0xa5, 0x25, 0xf0, 0x75, 0x23 };

    Packet p;
    IPV6Hdr ip6h;
    ThreadVars tv;
    DecodeThreadVars dtv;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));
    memset(&ip6h, 0, sizeof(IPV6Hdr));

    FlowInitConfig(FLOW_QUIET);
    DecodeIPV6(&tv, &dtv, &p, raw_ipv6, sizeof(raw_ipv6), NULL);
    FlowShutdown();

    if (p.icmpv6h == NULL) {
        SCLogDebug("ICMPv6 Unable to detect icmpv6 layer from ipv6");
        retval = 0;
        goto end;
    }

    SCLogDebug("type: %u code %u ID: %u seq: %u", ICMPV6_GET_TYPE(&p),
               ICMPV6_GET_CODE(&p),ICMPV6_GET_ID(&p), ICMPV6_GET_SEQ(&p));

    if (ICMPV6_GET_TYPE(&p) != 129 || ICMPV6_GET_CODE(&p) != 0 ||
        ICMPV6_GET_ID(&p) != 61477 || ICMPV6_GET_SEQ(&p) != 29987) {
        SCLogDebug("ICMPv6 Echo reply decode failed");
        retval = 0;
        goto end;
    }

    retval = 1;
end:
    return retval;
}

/** \test icmpv6 message type: parameter problem, invalid packet
 * \brief set the event ICMPV6_IPV6_UNKNOWN_VER properly when the embedded packet has an unknown version
 * \retval retval 0 = Error ; 1 = ok
 */
static int ICMPV6ParamProbTest02(void)
{
    int retval = 0;
    static uint8_t raw_ipv6[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x38, 0x3a, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x04, 0x00, 0xcc, 0x2a, 0x6d, 0x93, 0x0b, 0xdf,
        0x38, 0x70, 0x12, 0xb7, 0x00, 0x08, 0x3a, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x80, 0x00, 0x08, 0xb5, 0x99, 0xc3, 0xde, 0x40 };

    Packet p;
    IPV6Hdr ip6h;
    ThreadVars tv;
    DecodeThreadVars dtv;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));
    memset(&ip6h, 0, sizeof(IPV6Hdr));

    FlowInitConfig(FLOW_QUIET);
    DecodeIPV6(&tv, &dtv, &p, raw_ipv6, sizeof(raw_ipv6), NULL);
    FlowShutdown();

    if (p.icmpv6h == NULL) {
        SCLogDebug("ICMPv6 Unable to detect icmpv6 layer from ipv6");
        retval = 0;
        goto end;
    }

    if (ICMPV6_GET_TYPE(&p) != 4 || ICMPV6_GET_CODE(&p) != 0) {
        SCLogDebug("ICMPv6 Not processed at all");
        retval = 0;
        goto end;
    }

    if (!DECODER_ISSET_EVENT(&p, ICMPV6_IPV6_UNKNOWN_VER)) {
        SCLogDebug("ICMPv6 Error: Unknown embedded ipv6 version event not set");
        retval = 0;
        goto end;
    }

    retval = 1;
end:
    return retval;
}

/** \test icmpv6 message type: packet too big, invalid packet
 *  \brief Set the event ICMPV6_UNKNOWN_CODE if code is invalid for this type
 * \retval retval 0 = Error ; 1 = ok
 */
static int ICMPV6PktTooBigTest02(void)
{
    int retval = 0;
    static uint8_t raw_ipv6[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x30, 0x3a, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x02, 0x10, 0x5c, 0x7a, 0x00, 0x00, 0x05, 0x00,
        0x64, 0x14, 0xfd, 0xff, 0x00, 0x00, 0x3b, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };

    Packet p;
    IPV6Hdr ip6h;
    ThreadVars tv;
    DecodeThreadVars dtv;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));
    memset(&ip6h, 0, sizeof(IPV6Hdr));

    FlowInitConfig(FLOW_QUIET);
    DecodeIPV6(&tv, &dtv, &p, raw_ipv6, sizeof(raw_ipv6), NULL);
    FlowShutdown();

    if (p.icmpv6h == NULL) {
        SCLogDebug("ICMPv6 Unable to detect icmpv6 layer from ipv6");
        retval = 0;
        goto end;
    }

    if (!DECODER_ISSET_EVENT(&p, ICMPV6_UNKNOWN_CODE)) {
        SCLogDebug("ICMPv6 Error: Unknown code event not set");
        retval = 0;
        goto end;
    }

    retval = 1;
end:
    return retval;
}

/** \test icmpv6 message type: time exceed, invalid packet
 * \brief set the event ICMPV6_PKT_TOO_SMALL properly
 * \retval retval 0 = Error ; 1 = ok
 */
static int ICMPV6TimeExceedTest02(void)
{
    int retval = 0;
    static uint8_t raw_ipv6[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x03, 0x3a, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x02, 0x10, 0x5c };

    /* The icmpv6 header is broken in the checksum (so we dont have a complete header) */

    Packet p;
    IPV6Hdr ip6h;
    ThreadVars tv;
    DecodeThreadVars dtv;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));
    memset(&ip6h, 0, sizeof(IPV6Hdr));

    FlowInitConfig(FLOW_QUIET);
    DecodeIPV6(&tv, &dtv, &p, raw_ipv6, sizeof(raw_ipv6), NULL);
    FlowShutdown();

    if (!DECODER_ISSET_EVENT(&p, ICMPV6_PKT_TOO_SMALL)) {
        SCLogDebug("ICMPv6 Error: event packet too small not set");
        retval = 0;
        goto end;
    }

    retval = 1;
end:
    return retval;
}

/**\test icmpv6 message type: destination unreach, invalid packet
 * \brief The embedded packet header (ipv6) is truncated
 * \retval retval 0 = Error ; 1 = ok
 */
static int ICMPV6DestUnreachTest02(void)
{
    int retval = 0;
    static uint8_t raw_ipv6[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x2d, 0x3a, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x01, 0x00, 0x7b, 0x85, 0x00, 0x00, 0x00, 0x00,
        0x60, 0x4b, 0xe8, 0xbd, 0x00, 0x00, 0x3b, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00 };

    Packet p;
    IPV6Hdr ip6h;
    ThreadVars tv;
    DecodeThreadVars dtv;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));
    memset(&ip6h, 0, sizeof(IPV6Hdr));

    FlowInitConfig(FLOW_QUIET);
    DecodeIPV6(&tv, &dtv, &p, raw_ipv6, sizeof(raw_ipv6), NULL);
    FlowShutdown();

    if (!DECODER_ISSET_EVENT(&p, ICMPV6_IPV6_TRUNC_PKT)) {
        SCLogDebug("ICMPv6 Error: embedded ipv6 truncated packet event not set");
        retval = 0;
        goto end;
    }

    retval = 1;
end:
    return retval;
}

/**\test icmpv6 message type: echo request, invalid packet
 * \brief unknown code
 * \retval retval 0 = Error ; 1 = ok
 */
static int ICMPV6EchoReqTest02(void)
{
    int retval = 0;
    static uint8_t raw_ipv6[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x08, 0x3a,
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0xff, 0x02, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01,
        0xe5, 0xa5, 0x25, 0xf0, 0x75, 0x23 };

    Packet p;
    IPV6Hdr ip6h;
    ThreadVars tv;
    DecodeThreadVars dtv;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));
    memset(&ip6h, 0, sizeof(IPV6Hdr));

    FlowInitConfig(FLOW_QUIET);
    DecodeIPV6(&tv, &dtv, &p, raw_ipv6, sizeof(raw_ipv6), NULL);
    FlowShutdown();

    if (!DECODER_ISSET_EVENT(&p, ICMPV6_UNKNOWN_CODE)) {
        SCLogDebug("ICMPv6 Error: Unknown code event not set");
        retval = 0;
        goto end;
    }

    retval = 1;
end:
    return retval;
}

/**\test icmpv6 message type: echo reply, invalid packet
 * \brief unknown code
 * \retval retval 0 = Error ; 1 = ok
 */
static int ICMPV6EchoRepTest02(void)
{
    int retval = 0;
    static uint8_t raw_ipv6[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x08, 0x3a,
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0xff, 0x02, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x81, 0x01,
        0xe5, 0xa5, 0x25, 0xf0, 0x75, 0x23 };

    Packet p;
    IPV6Hdr ip6h;
    ThreadVars tv;
    DecodeThreadVars dtv;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));
    memset(&ip6h, 0, sizeof(IPV6Hdr));

    FlowInitConfig(FLOW_QUIET);
    DecodeIPV6(&tv, &dtv, &p, raw_ipv6, sizeof(raw_ipv6), NULL);
    FlowShutdown();

    if (!DECODER_ISSET_EVENT(&p, ICMPV6_UNKNOWN_CODE)) {
        SCLogDebug("ICMPv6 Error: Unknown code event not set");
        retval = 0;
        goto end;
    }

    retval = 1;
end:
    return retval;
}

#endif /* UNITTESTS */
/**
 * \brief Registers ICMPV6 unit tests
 * \todo More ICMPv6 tests
 */
void DecodeICMPV6RegisterTests(void)
{
#ifdef UNITTESTS
    UtRegisterTest("ICMPV6CalculateValidChecksumtest01", ICMPV6CalculateValidChecksumtest01, 1);
    UtRegisterTest("ICMPV6CalculateInValidChecksumtest02", ICMPV6CalculateInvalidChecksumtest02, 0);

    UtRegisterTest("ICMPV6ParamProbTest01 (Valid)", ICMPV6ParamProbTest01, 1);
    UtRegisterTest("ICMPV6DestUnreachTest01 (Valid)", ICMPV6DestUnreachTest01, 1);
    UtRegisterTest("ICMPV6PktTooBigTest01 (Valid)", ICMPV6PktTooBigTest01, 1);
    UtRegisterTest("ICMPV6TimeExceedTest01 (Valid)", ICMPV6TimeExceedTest01, 1);
    UtRegisterTest("ICMPV6EchoReqTest01 (Valid)", ICMPV6EchoReqTest01, 1);
    UtRegisterTest("ICMPV6EchoRepTest01 (Valid)", ICMPV6EchoRepTest01, 1);

    UtRegisterTest("ICMPV6ParamProbTest02 (Invalid)", ICMPV6ParamProbTest02, 1);
    UtRegisterTest("ICMPV6DestUnreachTest02 (Invalid)", ICMPV6DestUnreachTest02, 1);
    UtRegisterTest("ICMPV6PktTooBigTest02 (Invalid)", ICMPV6PktTooBigTest02, 1);
    UtRegisterTest("ICMPV6TimeExceedTest02 (Invalid)", ICMPV6TimeExceedTest02, 1);
    UtRegisterTest("ICMPV6EchoReqTest02 (Invalid)", ICMPV6EchoReqTest02, 1);
    UtRegisterTest("ICMPV6EchoRepTest02 (Invalid)", ICMPV6EchoRepTest02, 1);
#endif /* UNITTESTS */
}
