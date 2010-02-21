/* Copyright (c) 2008 Victor Julien <victor@inliniac.net> */

#include "suricata-common.h"

#include "decode.h"
#include "decode-events.h"
#include "decode-ipv4.h"
#include "decode-icmpv4.h"

#include "flow.h"

#include "util-unittest.h"
#include "util-debug.h"

/**
 * \brief Calculates the checksum for the ICMP packet
 *
 * \param pkt  Pointer to the start of the ICMP packet
 * \param hlen Total length of the ICMP packet(header + payload)
 *
 * \retval csum Checksum for the ICMP packet
 */
inline uint16_t ICMPV4CalculateChecksum(uint16_t *pkt, uint16_t tlen)
{
    uint16_t pad = 0;
    uint32_t csum = pkt[0];

    tlen -= 4;
    pkt += 2;

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
 * Note, this is the IP header, plus a bit of the original packet, not the whole thing!
 */
void DecodePartialIPV4( Packet* p, uint8_t* partial_packet, uint16_t len )
{
    /** Check the sizes, the header must fit at least */
    if (len < IPV4_HEADER_LEN) {
        SCLogDebug("DecodePartialIPV4: ICMPV4_IPV4_TRUNC_PKT");
        DECODER_SET_EVENT(p, ICMPV4_IPV4_TRUNC_PKT);
        return;
    }

    IPV4Hdr *icmp4_ip4h = (IPV4Hdr*)partial_packet;

    /** Check the embedded version */
    if (IPV4_GET_RAW_VER(icmp4_ip4h) != 4) {
        /** Check the embedded version */
        SCLogDebug("DecodePartialIPV4: ICMPv4 contains Unknown IPV4 version "
                   "ICMPV4_IPV4_UNKNOWN_VER");
        DECODER_SET_EVENT(p, ICMPV4_IPV4_UNKNOWN_VER);
        return;
    }

    /** We need to fill icmpv4vars */
    p->icmpv4vars.emb_ipv4h = icmp4_ip4h;

    /** Get the IP address from the contained packet */
    p->icmpv4vars.emb_ip4_src = IPV4_GET_RAW_IPSRC(icmp4_ip4h);
    p->icmpv4vars.emb_ip4_dst = IPV4_GET_RAW_IPDST(icmp4_ip4h);

    p->icmpv4vars.emb_ip4_hlen=IPV4_GET_RAW_HLEN(icmp4_ip4h) << 2;

    switch (IPV4_GET_RAW_IPPROTO(icmp4_ip4h)) {
        case IPPROTO_TCP:
            if (len >= IPV4_HEADER_LEN + TCP_HEADER_LEN ) {
                p->icmpv4vars.emb_tcph = (TCPHdr*)(partial_packet + IPV4_HEADER_LEN);
                p->icmpv4vars.emb_sport = p->icmpv4vars.emb_tcph->th_sport;
                p->icmpv4vars.emb_dport = p->icmpv4vars.emb_tcph->th_dport;

                SCLogDebug("DecodePartialIPV4: ICMPV4->IPV4->TCP header sport: "
                           "%"PRIu8" dport %"PRIu8"", p->icmpv4vars.emb_sport,
                            p->icmpv4vars.emb_dport);
            } else {
                SCLogDebug("DecodePartialIPV4: Warning, ICMPV4->IPV4->TCP "
                           "header Didn't fit in the packet!");
                p->icmpv4vars.emb_sport = 0;
                p->icmpv4vars.emb_dport = 0;
            }

            break;
        case IPPROTO_UDP:
            if (len >= IPV4_HEADER_LEN + UDP_HEADER_LEN ) {
                p->icmpv4vars.emb_udph = (UDPHdr*)(partial_packet + IPV4_HEADER_LEN);
                p->icmpv4vars.emb_sport = p->icmpv4vars.emb_udph->uh_sport;
                p->icmpv4vars.emb_dport = p->icmpv4vars.emb_udph->uh_dport;

                SCLogDebug("DecodePartialIPV4: ICMPV4->IPV4->UDP header sport: "
                           "%"PRIu8" dport %"PRIu8"", p->icmpv4vars.emb_sport,
                            p->icmpv4vars.emb_dport);
            } else {
                SCLogDebug("DecodePartialIPV4: Warning, ICMPV4->IPV4->UDP "
                           "header Didn't fit in the packet!");
                p->icmpv4vars.emb_sport = 0;
                p->icmpv4vars.emb_dport = 0;
            }

            break;
        case IPPROTO_ICMP:
            p->icmpv4vars.emb_icmpv4h = (ICMPV4Hdr*)(partial_packet + IPV4_HEADER_LEN);
            p->icmpv4vars.emb_sport = 0;
            p->icmpv4vars.emb_dport = 0;

            SCLogDebug("DecodePartialIPV4: ICMPV4->IPV4->ICMP header");

            break;
    }

    /* debug print */
#ifdef DEBUG
    char s[16], d[16];
    inet_ntop(AF_INET, &(p->icmpv4vars.emb_ip4_src), s, sizeof(s));
    inet_ntop(AF_INET, &(p->icmpv4vars.emb_ip4_dst), d, sizeof(d));
    SCLogDebug("ICMPv4 embedding IPV4 %s->%s - PROTO: %" PRIu32 " ID: %" PRIu32 "", s,d,
            IPV4_GET_RAW_IPPROTO(icmp4_ip4h), IPV4_GET_RAW_IPID(icmp4_ip4h));
#endif

    return;

}

/** DecodeICMPV4
 *  \brief Main ICMPv4 decoding function
 */
void DecodeICMPV4(ThreadVars *tv, DecodeThreadVars *dtv, Packet *p, uint8_t *pkt, uint16_t len, PacketQueue *pq)
{
    SCPerfCounterIncr(dtv->counter_icmpv4, tv->sc_perf_pca);

    if (len < ICMPV4_HEADER_LEN) {
        DECODER_SET_EVENT(p,ICMPV4_PKT_TOO_SMALL);
        return;
    }

    p->icmpv4h = (ICMPV4Hdr *)pkt;

    SCLogDebug("ICMPV4 TYPE %" PRIu32 " CODE %" PRIu32 "", p->icmpv4h->type, p->icmpv4h->code);

    p->proto = IPPROTO_ICMP;
    p->type = p->icmpv4h->type;
    p->code = p->icmpv4h->code;

    ICMPV4ExtHdr* icmp4eh = (ICMPV4ExtHdr*) p->icmpv4h;

    switch (p->icmpv4h->type)
    {
        case ICMP_ECHOREPLY:
            p->icmpv4vars.id=icmp4eh->id;
            p->icmpv4vars.seq=icmp4eh->seq;
            if (p->icmpv4h->code!=0) {
                DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_CODE);
            }
            break;

        case ICMP_DEST_UNREACH:
            if (p->icmpv4h->code>NR_ICMP_UNREACH) {
                DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_CODE);
            } else {
                // parse IP header plus 64 bytes
                if (len >= ICMPV4_HEADER_PKT_OFFSET)
                    DecodePartialIPV4( p, (uint8_t*) (pkt + ICMPV4_HEADER_PKT_OFFSET), len - ICMPV4_HEADER_PKT_OFFSET );
            }
            break;

        case ICMP_SOURCE_QUENCH:
            if (p->icmpv4h->code!=0) {
                DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_CODE);
            } else {
                // parse IP header plus 64 bytes
                if (len >= ICMPV4_HEADER_PKT_OFFSET)
                    DecodePartialIPV4( p, (uint8_t*) (pkt + ICMPV4_HEADER_PKT_OFFSET), len - ICMPV4_HEADER_PKT_OFFSET  );
            }
            break;

        case ICMP_REDIRECT:
            if (p->icmpv4h->code>ICMP_REDIR_HOSTTOS) {
                DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_CODE);
            } else {
                // parse IP header plus 64 bytes
                if (len >= ICMPV4_HEADER_PKT_OFFSET)
                    DecodePartialIPV4( p, (uint8_t*) (pkt + ICMPV4_HEADER_PKT_OFFSET), len - ICMPV4_HEADER_PKT_OFFSET );
            }
            break;

        case ICMP_ECHO:
            p->icmpv4vars.id=icmp4eh->id;
            p->icmpv4vars.seq=icmp4eh->seq;
            if (p->icmpv4h->code!=0) {
                DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_CODE);
            }
            break;

        case ICMP_TIME_EXCEEDED:
            if (p->icmpv4h->code>ICMP_EXC_FRAGTIME) {
                DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_CODE);
            } else {
                // parse IP header plus 64 bytes
                if (len >= ICMPV4_HEADER_PKT_OFFSET)
                    DecodePartialIPV4( p, (uint8_t*) (pkt + ICMPV4_HEADER_PKT_OFFSET), len - ICMPV4_HEADER_PKT_OFFSET );
            }
            break;

        case ICMP_PARAMETERPROB:
            if (p->icmpv4h->code!=0) {
                DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_CODE);
            } else {
                // parse IP header plus 64 bytes
                if (len >= ICMPV4_HEADER_PKT_OFFSET)
                    DecodePartialIPV4( p, (uint8_t*) (pkt + ICMPV4_HEADER_PKT_OFFSET), len - ICMPV4_HEADER_PKT_OFFSET );
            }
            break;

        case ICMP_TIMESTAMP:
            p->icmpv4vars.id=icmp4eh->id;
            p->icmpv4vars.seq=icmp4eh->seq;
            if (p->icmpv4h->code!=0) {
                DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_CODE);
            }
            break;

        case ICMP_TIMESTAMPREPLY:
            p->icmpv4vars.id=icmp4eh->id;
            p->icmpv4vars.seq=icmp4eh->seq;
            if (p->icmpv4h->code!=0) {
                DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_CODE);
            }
            break;

        case ICMP_INFO_REQUEST:
            p->icmpv4vars.id=icmp4eh->id;
            p->icmpv4vars.seq=icmp4eh->seq;
            if (p->icmpv4h->code!=0) {
                DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_CODE);
            }
            break;

        case ICMP_INFO_REPLY:
            p->icmpv4vars.id=icmp4eh->id;
            p->icmpv4vars.seq=icmp4eh->seq;
            if (p->icmpv4h->code!=0) {
                DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_CODE);
            }
            break;

        case ICMP_ADDRESS:
            p->icmpv4vars.id=icmp4eh->id;
            p->icmpv4vars.seq=icmp4eh->seq;
            if (p->icmpv4h->code!=0) {
                DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_CODE);
            }
            break;

        case ICMP_ADDRESSREPLY:
            p->icmpv4vars.id=icmp4eh->id;
            p->icmpv4vars.seq=icmp4eh->seq;
            if (p->icmpv4h->code!=0) {
                DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_CODE);
            }
            break;

        default:
            DECODER_SET_EVENT(p,ICMPV4_UNKNOWN_TYPE);

    }

    /* Flow is an integral part of us */
    FlowHandlePacket(tv, p);

    return;
}

#ifdef UNITTESTS

/** DecodeICMPV4test01
 *  \brief
 *  \retval 1 Expected test value
 */
static int DecodeICMPV4test01(void) {
    uint8_t raw_icmpv4[] = {
        0x08, 0x00, 0x78, 0x47, 0xfc, 0x55, 0x00, 0x04,
        0x52, 0xab, 0x86, 0x4a, 0x84, 0x50, 0x0e, 0x00,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab };
    Packet p;
    ThreadVars tv;
    DecodeThreadVars dtv;
    int ret = 0;
    IPV4Hdr ip4h;

    memset(&ip4h, 0, sizeof(IPV4Hdr));
    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&ip4h, 0, sizeof(IPV4Hdr));
    memset(&dtv, 0, sizeof(DecodeThreadVars));

    FlowInitConfig(FLOW_QUIET);

    p.src.family = AF_INET;
    p.dst.family = AF_INET;
    p.src.addr_data32[0] = 0x01020304;
    p.dst.addr_data32[0] = 0x04030201;

    ip4h.ip_src.s_addr = p.src.addr_data32[0];
    ip4h.ip_dst.s_addr = p.dst.addr_data32[0];
    p.ip4h = &ip4h;

    DecodeICMPV4(&tv, &dtv, &p, raw_icmpv4, sizeof(raw_icmpv4), NULL);

    if (NULL!=p.icmpv4h) {
        if (p.icmpv4h->type==8 && p.icmpv4h->code==0) {
            ret = 1;
        }
    }

    FlowShutdown();
    return ret;
}

/** DecodeICMPV4test02
 *  \brief
 *  \retval 1 Expected test value
 */
static int DecodeICMPV4test02(void) {
    uint8_t raw_icmpv4[] = {
        0x00, 0x00, 0x57, 0x64, 0xfb, 0x55, 0x00, 0x03,
        0x43, 0xab, 0x86, 0x4a, 0xf6, 0x49, 0x02, 0x00,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f };
    Packet p;
    ThreadVars tv;
    DecodeThreadVars dtv;
    int ret = 0;
    IPV4Hdr ip4h;

    memset(&ip4h, 0, sizeof(IPV4Hdr));
    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));

    FlowInitConfig(FLOW_QUIET);

    p.src.family = AF_INET;
    p.dst.family = AF_INET;
    p.src.addr_data32[0] = 0x01020304;
    p.dst.addr_data32[0] = 0x04030201;

    ip4h.ip_src.s_addr = p.src.addr_data32[0];
    ip4h.ip_dst.s_addr = p.dst.addr_data32[0];
    p.ip4h = &ip4h;

    DecodeICMPV4(&tv, &dtv, &p, raw_icmpv4, sizeof(raw_icmpv4), NULL);

    if (NULL!=p.icmpv4h) {
        if (p.icmpv4h->type==0 && p.icmpv4h->code==0) {
            ret = 1;
        }
    }

    FlowShutdown();
    return ret;
}

/** DecodeICMPV4test03
 *  \brief  TTL exceeded
 *  \retval Expected test value: 1
 */
static int DecodeICMPV4test03(void) {
    uint8_t raw_icmpv4[] = {
        0x0b, 0x00, 0x6a, 0x3d, 0x00, 0x00, 0x00, 0x00,
        0x45, 0x00, 0x00, 0x3c, 0x64, 0x15, 0x00, 0x00,
        0x01, 0x11, 0xde, 0xfd, 0xc0, 0xa8, 0x01, 0x0d,
        0xd1, 0x55, 0xe3, 0x93, 0x8b, 0x12, 0x82, 0xaa,
        0x00, 0x28, 0x7c, 0xdd };
    Packet p;
    ThreadVars tv;
    DecodeThreadVars dtv;
    int ret = 0;
    IPV4Hdr ip4h;

    memset(&ip4h, 0, sizeof(IPV4Hdr));
    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));

    FlowInitConfig(FLOW_QUIET);

    p.src.family = AF_INET;
    p.dst.family = AF_INET;
    p.src.addr_data32[0] = 0x01020304;
    p.dst.addr_data32[0] = 0x04030201;

    ip4h.ip_src.s_addr = p.src.addr_data32[0];
    ip4h.ip_dst.s_addr = p.dst.addr_data32[0];
    p.ip4h = &ip4h;

    DecodeICMPV4(&tv, &dtv, &p, raw_icmpv4, sizeof(raw_icmpv4), NULL);

    if (NULL == p.icmpv4h) {
        goto end;
    }

    // check it's type 11 code 0
    if (p.icmpv4h->type!=11 || p.icmpv4h->code!=0) {
        goto end;
    }

    // check it's source port 4747 to port 43650
    if (p.icmpv4vars.emb_sport != 4747 || p.icmpv4vars.emb_dport != 43650) {
        goto end;
    }

    // check the src,dst IPs contained inside
    char s[16], d[16];

    inet_ntop(AF_INET, &(p.icmpv4vars.emb_ip4_src), s, sizeof(s));
    inet_ntop(AF_INET, &(p.icmpv4vars.emb_ip4_dst), d, sizeof(d));

    // ICMPv4 embedding IPV4 192.168.1.13->209.85.227.147 pass
    if (strcmp(s, "192.168.1.13") == 0 && strcmp(d, "209.85.227.147") == 0) {
        ret = 1;
    }

end:
    FlowShutdown();
    return ret;
}

/** DecodeICMPV4test04
 *  \brief dest. unreachable, administratively prohibited
 *  \retval 1 Expected test value
 */
static int DecodeICMPV4test04(void) {
    uint8_t raw_icmpv4[] = {
        0x03, 0x0a, 0x36, 0xc3, 0x00, 0x00, 0x00, 0x00,
        0x45, 0x00, 0x00, 0x3c, 0x62, 0xee, 0x40, 0x00,
        0x33, 0x06, 0xb4, 0x8f, 0xc0, 0xa8, 0x01, 0x0d,
        0x58, 0x60, 0x16, 0x29, 0xb1, 0x0a, 0x00, 0x32,
        0x3e, 0x36, 0x38, 0x7c, 0x00, 0x00, 0x00, 0x00,
        0xa0, 0x02, 0x16, 0xd0, 0x72, 0x04, 0x00, 0x00,
        0x02, 0x04, 0x05, 0x8a, 0x04, 0x02, 0x08, 0x0a };
    Packet p;
    ThreadVars tv;
    DecodeThreadVars dtv;
    int ret = 0;
    IPV4Hdr ip4h;

    memset(&ip4h, 0, sizeof(IPV4Hdr));
    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));

    FlowInitConfig(FLOW_QUIET);

    p.src.family = AF_INET;
    p.dst.family = AF_INET;
    p.src.addr_data32[0] = 0x01020304;
    p.dst.addr_data32[0] = 0x04030201;

    ip4h.ip_src.s_addr = p.src.addr_data32[0];
    ip4h.ip_dst.s_addr = p.dst.addr_data32[0];
    p.ip4h = &ip4h;

    DecodeICMPV4(&tv, &dtv, &p, raw_icmpv4, sizeof(raw_icmpv4), NULL);

    if (NULL == p.icmpv4h) {
        goto end;
    }

    // check the type,code pair is correct - type 3, code 10
    if (p.icmpv4h->type != 3 || p.icmpv4h->code != 10) {
        goto end;
    }

    // check it's src port 2737 to dst port 12800
    if (p.icmpv4vars.emb_sport != 2737 || p.icmpv4vars.emb_dport != 12800) {
        goto end;
    }

    // check the src,dst IPs contained inside
    char s[16], d[16];

    inet_ntop(AF_INET, &(p.icmpv4vars.emb_ip4_src), s, sizeof(s));
    inet_ntop(AF_INET, &(p.icmpv4vars.emb_ip4_dst), d, sizeof(d));

    // ICMPv4 embedding IPV4 192.168.1.13->88.96.22.41
    if (strcmp(s, "192.168.1.13") == 0 && strcmp(d, "88.96.22.41") == 0) {
        ret = 1;
    }

end:
    FlowShutdown();
    return ret;
}

static int ICMPV4CalculateValidChecksumtest05(void) {
    uint16_t csum = 0;

    uint8_t raw_icmpv4[] = {
        0x08, 0x00, 0xab, 0x9b, 0x7f, 0x2b, 0x05, 0x2c,
        0x3f, 0x72, 0x93, 0x4a, 0x00, 0x4d, 0x0a, 0x00,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};

    csum = *( ((uint16_t *)raw_icmpv4) + 1);
    return (csum == ICMPV4CalculateChecksum((uint16_t *)raw_icmpv4, sizeof(raw_icmpv4)));
}

static int ICMPV4CalculateInvalidChecksumtest06(void) {
    uint16_t csum = 0;

    uint8_t raw_icmpv4[] = {
        0x08, 0x00, 0xab, 0x9b, 0x7f, 0x2b, 0x05, 0x2c,
        0x3f, 0x72, 0x93, 0x4a, 0x00, 0x4d, 0x0a, 0x00,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x38};

    csum = *( ((uint16_t *)raw_icmpv4) + 1);
    return (csum == ICMPV4CalculateChecksum((uint16_t *)raw_icmpv4, sizeof(raw_icmpv4)));
}

static int ICMPV4InvalidType07(void) {

    uint8_t raw_icmpv4[] = {
        0xff, 0x00, 0xab, 0x9b, 0x7f, 0x2b, 0x05, 0x2c,
        0x3f, 0x72, 0x93, 0x4a, 0x00, 0x4d, 0x0a, 0x00,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x38};

    Packet p;
    ThreadVars tv;
    DecodeThreadVars dtv;
    int ret = 0;
    IPV4Hdr ip4h;

    memset(&ip4h, 0, sizeof(IPV4Hdr));
    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));

    FlowInitConfig(FLOW_QUIET);

    p.src.family = AF_INET;
    p.dst.family = AF_INET;
    p.src.addr_data32[0] = 0x01020304;
    p.dst.addr_data32[0] = 0x04030201;

    ip4h.ip_src.s_addr = p.src.addr_data32[0];
    ip4h.ip_dst.s_addr = p.dst.addr_data32[0];
    p.ip4h = &ip4h;

    DecodeICMPV4(&tv, &dtv, &p, raw_icmpv4, sizeof(raw_icmpv4), NULL);

    if(DECODER_ISSET_EVENT(&p,ICMPV4_UNKNOWN_TYPE)) {
        ret = 1;
    }

    FlowShutdown();
    return ret;
}

/** DecodeICMPV4test08
 *  \brief
 *  \retval 1 Expected test value - what we really want is not to segfault
 */
static int DecodeICMPV4test08(void) {
    uint8_t raw_icmpv4[] = {
        0x08, 0x00, 0x78, 0x47, 0xfc, 0x55, 0x00, 0x00
    };
    Packet p;
    ThreadVars tv;
    DecodeThreadVars dtv;
    int ret = 0;
    IPV4Hdr ip4h;

    memset(&ip4h, 0, sizeof(IPV4Hdr));
    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&dtv, 0, sizeof(DecodeThreadVars));

    FlowInitConfig(FLOW_QUIET);

    p.src.family = AF_INET;
    p.dst.family = AF_INET;
    p.src.addr_data32[0] = 0x01020304;
    p.dst.addr_data32[0] = 0x04030201;

    ip4h.ip_src.s_addr = p.src.addr_data32[0];
    ip4h.ip_dst.s_addr = p.dst.addr_data32[0];
    p.ip4h = &ip4h;

    DecodeICMPV4(&tv, &dtv, &p, raw_icmpv4, sizeof(raw_icmpv4), NULL);

    if (NULL!=p.icmpv4h) {
        if (p.icmpv4h->type==8 && p.icmpv4h->code==0) {
            ret = 1;
        }
    }

    FlowShutdown();
    return ret;
}
#endif /* UNITTESTS */

/**
 * \brief Registers ICMPV4 unit test
 */
void DecodeICMPV4RegisterTests(void) {
#ifdef UNITTESTS
    UtRegisterTest("DecodeICMPV4test01", DecodeICMPV4test01, 1);
    UtRegisterTest("DecodeICMPV4test02", DecodeICMPV4test02, 1);
    UtRegisterTest("DecodeICMPV4test03", DecodeICMPV4test03, 1);
    UtRegisterTest("DecodeICMPV4test04", DecodeICMPV4test04, 1);
    UtRegisterTest("ICMPV4CalculateValidChecksumtest05",
                   ICMPV4CalculateValidChecksumtest05, 1);
    UtRegisterTest("ICMPV4CalculateInvalidChecksumtest06",
                   ICMPV4CalculateInvalidChecksumtest06, 0);
    UtRegisterTest("DecodeICMPV4InvalidType", ICMPV4InvalidType07, 1);
    UtRegisterTest("DecodeICMPV4test08", DecodeICMPV4test08, 1);
#endif /* UNITTESTS */
}
