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
 * Live pcap packet acquisition support
 */

#if LIBPCAP_VERSION_MAJOR == 1
#include <pcap/pcap.h>
#else
#include <pcap.h>
#endif

#include "suricata-common.h"
#include "suricata.h"
#include "decode.h"
#include "packet-queue.h"
#include "threads.h"
#include "threadvars.h"
#include "tm-queuehandlers.h"
#include "tm-modules.h"
#include "tm-threads.h"
#include "source-pcap.h"
#include "conf.h"
#include "util-debug.h"
#include "util-error.h"
#include "util-privs.h"
#include "tmqh-packetpool.h"

extern uint8_t suricata_ctl_flags;
extern int max_pending_packets;

static int pcap_max_read_packets = 0;

/** max packets < 65536 */
#define PCAP_FILE_MAX_PKTS 256

/**
 * \brief Structure to hold thread specific variables.
 */
typedef struct PcapThreadVars_
{
    /* thread specific handle */
    pcap_t *pcap_handle;

    /* thread specific bpf */
    struct bpf_program filter;

    /* data link type for the thread */
    int datalink;

    /* counters */
    uint32_t pkts;
    uint64_t bytes;
    uint32_t errs;

    /* pcap buffer size */
    int pcap_buffer_size;

    ThreadVars *tv;

    Packet *in_p;

    Packet *array[PCAP_FILE_MAX_PKTS];
    uint16_t array_idx;
} PcapThreadVars;

TmEcode ReceivePcap(ThreadVars *, Packet *, void *, PacketQueue *, PacketQueue *);
TmEcode ReceivePcapThreadInit(ThreadVars *, void *, void **);
void ReceivePcapThreadExitStats(ThreadVars *, void *);
TmEcode ReceivePcapThreadDeinit(ThreadVars *, void *);

TmEcode DecodePcapThreadInit(ThreadVars *, void *, void **);
TmEcode DecodePcap(ThreadVars *, Packet *, void *, PacketQueue *, PacketQueue *);

/**
 * \brief Registration Function for RecievePcap.
 * \todo Unit tests are needed for this module.
 */
void TmModuleReceivePcapRegister (void) {
    tmm_modules[TMM_RECEIVEPCAP].name = "ReceivePcap";
    tmm_modules[TMM_RECEIVEPCAP].ThreadInit = ReceivePcapThreadInit;
    tmm_modules[TMM_RECEIVEPCAP].Func = ReceivePcap;
    tmm_modules[TMM_RECEIVEPCAP].ThreadExitPrintStats = ReceivePcapThreadExitStats;
    tmm_modules[TMM_RECEIVEPCAP].ThreadDeinit = NULL;
    tmm_modules[TMM_RECEIVEPCAP].RegisterTests = NULL;
    tmm_modules[TMM_RECEIVEPCAP].cap_flags = SC_CAP_NET_RAW;
}

/**
 * \brief Registration Function for DecodePcap.
 * \todo Unit tests are needed for this module.
 */
void TmModuleDecodePcapRegister (void) {
    tmm_modules[TMM_DECODEPCAP].name = "DecodePcap";
    tmm_modules[TMM_DECODEPCAP].ThreadInit = DecodePcapThreadInit;
    tmm_modules[TMM_DECODEPCAP].Func = DecodePcap;
    tmm_modules[TMM_DECODEPCAP].ThreadExitPrintStats = NULL;
    tmm_modules[TMM_DECODEPCAP].ThreadDeinit = NULL;
    tmm_modules[TMM_DECODEPCAP].RegisterTests = NULL;
    tmm_modules[TMM_DECODEPCAP].cap_flags = 0;
}

/**
 * \brief Pcap callback function.
 *
 * This function fills in our packet structure from libpcap.
 * From here the packets are picked up by the  DecodePcap thread.
 *
 * \param user pointer to PcapThreadVars passed from pcap_dispatch
 * \param h pointer to pcap packet header
 * \param pkt pointer to raw packet data
 */
void PcapCallback(char *user, struct pcap_pkthdr *h, u_char *pkt) {
    SCLogDebug("user %p, h %p, pkt %p", user, h, pkt);
    PcapThreadVars *ptv = (PcapThreadVars *)user;

    Packet *p = NULL;
    if (ptv->array_idx == 0) {
        p = ptv->in_p;
    } else {
        p = PacketGetFromQueueOrAlloc();
    }

    if (p == NULL) {
        SCReturn;
    }

    p->ts.tv_sec = h->ts.tv_sec;
    p->ts.tv_usec = h->ts.tv_usec;

    ptv->pkts++;
    ptv->bytes += h->caplen;

    p->datalink = ptv->datalink;
    p->pktlen = h->caplen;
    memcpy(p->pkt, pkt, p->pktlen);
    SCLogDebug("p->pktlen: %" PRIu32 " (pkt %02x, p->pkt %02x)", p->pktlen, *pkt, *p->pkt);

    /* store the packet in our array */
    ptv->array[ptv->array_idx] = p;
    ptv->array_idx++;
}

/**
 * \brief Recieves packets from an interface via libpcap.
 *
 *  This function recieves packets from an interface and passes
 *  the packet on to the pcap callback function.
 *
 * \param tv pointer to ThreadVars
 * \param data pointer that gets cast into PcapThreadVars for ptv
 * \param pq pointer to the PacketQueue (not used here but part of the api)
 * \retval TM_ECODE_FAILED on failure and TM_ECODE_OK on success
 */
TmEcode ReceivePcap(ThreadVars *tv, Packet *p, void *data, PacketQueue *pq, PacketQueue *postpq) {
    SCEnter();
    uint16_t packet_q_len = 0;

    PcapThreadVars *ptv = (PcapThreadVars *)data;

    /* make sure we have at least one packet in the packet pool, to prevent
     * us from alloc'ing packets at line rate */
    while (packet_q_len == 0) {
        packet_q_len = PacketPoolSize();
        if (packet_q_len == 0) {
            PacketPoolWait();
        }
    }

    if (postpq == NULL)
        pcap_max_read_packets = 1;

    ptv->array_idx = 0;
    ptv->in_p = p;

    int r = 0;
    while (r == 0) {
        r = pcap_dispatch(ptv->pcap_handle, (pcap_max_read_packets < packet_q_len) ? pcap_max_read_packets : packet_q_len,
            (pcap_handler)PcapCallback, (u_char *)ptv);
        if (suricata_ctl_flags != 0) {
            break;
        }
    }

    uint16_t cnt = 0;
    for (cnt = 0; cnt < ptv->array_idx; cnt++) {
        Packet *pp = ptv->array[cnt];

        /* enqueue all but the first in the postpq, the first
         * pkt is handled by the tv "out handler" */
        if (cnt > 0) {
            PacketEnqueue(postpq, pp);
        }
    }

    if (r < 0) {
        SCLogError(SC_ERR_PCAP_DISPATCH, "error code %" PRId32 " %s",
                r, pcap_geterr(ptv->pcap_handle));

        EngineStop();
        SCReturnInt(TM_ECODE_FAILED);
    }

    if (suricata_ctl_flags != 0) {
        SCReturnInt(TM_ECODE_FAILED);
    }

    SCReturnInt(TM_ECODE_OK);
}

/**
 * \brief Init function for ReceivePcap.
 *
 * This is a setup function for recieving packets
 * via libpcap. There are two versions of this function
 * depending on the major version of libpcap used.
 * For versions prior to 1.x we use open_pcap_live,
 * for versions 1.x and greater we use pcap_create + pcap_activate.
 *
 * \param tv pointer to ThreadVars
 * \param initdata pointer to the interface passed from the user
 * \param data pointer gets populated with PcapThreadVars
 *
 * \todo Create a general pcap setup function.
 */
#if LIBPCAP_VERSION_MAJOR == 1
TmEcode ReceivePcapThreadInit(ThreadVars *tv, void *initdata, void **data) {
    SCEnter();
    char *tmpbpfstring;

    /* use max_pending_packets as pcap read size unless it's bigger than
     * our size limit */
    pcap_max_read_packets = (PCAP_FILE_MAX_PKTS < max_pending_packets) ?
        PCAP_FILE_MAX_PKTS : max_pending_packets;

    if (initdata == NULL) {
        SCLogError(SC_ERR_INVALID_ARGUMENT, "initdata == NULL");
        SCReturnInt(TM_ECODE_FAILED);
    }

    PcapThreadVars *ptv = SCMalloc(sizeof(PcapThreadVars));
    if (ptv == NULL)
        SCReturnInt(TM_ECODE_FAILED);
    memset(ptv, 0, sizeof(PcapThreadVars));

    ptv->tv = tv;

    SCLogInfo("using interface %s", (char *)initdata);

    /* XXX create a general pcap setup function */
    char errbuf[PCAP_ERRBUF_SIZE];
    ptv->pcap_handle = pcap_create((char *)initdata, errbuf);
    if (ptv->pcap_handle == NULL) {
        SCLogError(SC_ERR_PCAP_CREATE, "Coudn't create a new pcap handler, error %s", pcap_geterr(ptv->pcap_handle));
        SCFree(ptv);
        SCReturnInt(TM_ECODE_FAILED);
    }

    /* set Snaplen, Promisc, and Timeout. Must be called before pcap_activate */
    int pcap_set_snaplen_r = pcap_set_snaplen(ptv->pcap_handle,LIBPCAP_SNAPLEN);
    //printf("ReceivePcapThreadInit: pcap_set_snaplen(%p) returned %" PRId32 "\n", ptv->pcap_handle, pcap_set_snaplen_r);
    if (pcap_set_snaplen_r != 0) {
        SCLogError(SC_ERR_PCAP_SET_SNAPLEN, "Couldn't set snaplen, error: %s", pcap_geterr(ptv->pcap_handle));
        SCFree(ptv);
        SCReturnInt(TM_ECODE_FAILED);
    }

    int pcap_set_promisc_r = pcap_set_promisc(ptv->pcap_handle,LIBPCAP_PROMISC);
    //printf("ReceivePcapThreadInit: pcap_set_promisc(%p) returned %" PRId32 "\n", ptv->pcap_handle, pcap_set_promisc_r);
    if (pcap_set_promisc_r != 0) {
        SCLogError(SC_ERR_PCAP_SET_PROMISC, "Couldn't set promisc mode, error %s", pcap_geterr(ptv->pcap_handle));
        SCFree(ptv);
        SCReturnInt(TM_ECODE_FAILED);
    }

    int pcap_set_timeout_r = pcap_set_timeout(ptv->pcap_handle,LIBPCAP_COPYWAIT);
    //printf("ReceivePcapThreadInit: pcap_set_timeout(%p) returned %" PRId32 "\n", ptv->pcap_handle, pcap_set_timeout_r);
    if (pcap_set_timeout_r != 0) {
        SCLogError(SC_ERR_PCAP_SET_TIMEOUT, "Problems setting timeout, error %s", pcap_geterr(ptv->pcap_handle));
        SCFree(ptv);
        SCReturnInt(TM_ECODE_FAILED);
    }
#ifdef HAVE_PCAP_SET_BUFF
    char *tmppcapbuffsize;
    /* set pcap buffer size if specified and supported. Must be done prior to activating the handle */
    if (ConfGet("pcap.buffer-size", &tmppcapbuffsize) == 1){
        if (atoi(tmppcapbuffsize) >= 0 && atoi(tmppcapbuffsize) <= INT_MAX) {
            ptv->pcap_buffer_size = (int)atoi(tmppcapbuffsize);
            SCLogInfo("Going to use pcap buffer size of %" PRId32 "", ptv->pcap_buffer_size);

            int pcap_set_buffer_size_r = pcap_set_buffer_size(ptv->pcap_handle,ptv->pcap_buffer_size);
            //printf("ReceivePcapThreadInit: pcap_set_timeout(%p) returned %" PRId32 "\n", ptv->pcap_handle, pcap_set_buffer_size_r);
            if (pcap_set_buffer_size_r != 0) {
                SCLogError(SC_ERR_PCAP_SET_BUFF_SIZE, "Problems setting pcap buffer size, error %s", pcap_geterr(ptv->pcap_handle));
                SCFree(ptv);
                SCReturnInt(TM_ECODE_FAILED);
            }

        }
    }
#endif /* HAVE_PCAP_SET_BUFF */

    /* activate the handle */
    int pcap_activate_r = pcap_activate(ptv->pcap_handle);
    //printf("ReceivePcapThreadInit: pcap_activate(%p) returned %" PRId32 "\n", ptv->pcap_handle, pcap_activate_r);
    if (pcap_activate_r != 0) {
        SCLogError(SC_ERR_PCAP_ACTIVATE_HANDLE, "Couldn't activate the pcap handler, error %s", pcap_geterr(ptv->pcap_handle));
        SCFree(ptv);
        SCReturnInt(TM_ECODE_FAILED);
    }

    /* set bpf filter if we have one */
    if (ConfGet("bpf-filter", &tmpbpfstring) != 1) {
        SCLogDebug("could not get bpf or none specified");
    } else {
        SCLogInfo("using bpf-filter \"%s\"", tmpbpfstring);

        if(pcap_compile(ptv->pcap_handle,&ptv->filter,tmpbpfstring,1,0) < 0) {
            SCLogError(SC_ERR_BPF,"bpf compilation error %s",pcap_geterr(ptv->pcap_handle));
            SCFree(ptv);
            return TM_ECODE_FAILED;
        }

        if(pcap_setfilter(ptv->pcap_handle,&ptv->filter) < 0) {
            SCLogError(SC_ERR_BPF,"could not set bpf filter %s",pcap_geterr(ptv->pcap_handle));
            SCFree(ptv);
            return TM_ECODE_FAILED;
        }
    }

    ptv->datalink = pcap_datalink(ptv->pcap_handle);

    *data = (void *)ptv;
    SCReturnInt(TM_ECODE_OK);
}
#else /* implied LIBPCAP_VERSION_MAJOR == 0 */
TmEcode ReceivePcapThreadInit(ThreadVars *tv, void *initdata, void **data) {
    SCEnter();

    char *tmpbpfstring;

    /* use max_pending_packets as pcap read size unless it's bigger than
     * our size limit */
    pcap_max_read_packets = (PCAP_FILE_MAX_PKTS < max_pending_packets) ?
        PCAP_FILE_MAX_PKTS : max_pending_packets;

    if (initdata == NULL) {
        SCLogError(SC_ERR_INVALID_ARGUMENT, "initdata == NULL");
        SCReturnInt(TM_ECODE_FAILED);
    }

    PcapThreadVars *ptv = SCMalloc(sizeof(PcapThreadVars));
    if (ptv == NULL)
        SCReturnInt(TM_ECODE_FAILED);
    memset(ptv, 0, sizeof(PcapThreadVars));

    ptv->tv = tv;

    SCLogInfo("using interface %s", (char *)initdata);

    char errbuf[PCAP_ERRBUF_SIZE] = "";
    ptv->pcap_handle = pcap_open_live((char *)initdata, LIBPCAP_SNAPLEN,
                                        LIBPCAP_PROMISC, LIBPCAP_COPYWAIT, errbuf);
    if (ptv->pcap_handle == NULL) {
        SCLogError(SC_ERR_PCAP_OPEN_LIVE, "Problem creating pcap handler for live mode, error %s", errbuf);
        SCReturnInt(TM_ECODE_FAILED);
    }

    /* set bpf filter if we have one */
    if (ConfGet("bpf-filter", &tmpbpfstring) != 1) {
        SCLogDebug("could not get bpf or none specified");
    } else {
        SCLogInfo("using bpf-filter \"%s\"", tmpbpfstring);

        if(pcap_compile(ptv->pcap_handle,&ptv->filter,tmpbpfstring,1,0) < 0) {
            SCLogError(SC_ERR_BPF,"bpf compilation error %s",pcap_geterr(ptv->pcap_handle));
            return TM_ECODE_FAILED;
        }

        if(pcap_setfilter(ptv->pcap_handle,&ptv->filter) < 0) {
            SCLogError(SC_ERR_BPF,"could not set bpf filter %s",pcap_geterr(ptv->pcap_handle));
            return TM_ECODE_FAILED;
        }
    }


    ptv->datalink = pcap_datalink(ptv->pcap_handle);

    *data = (void *)ptv;
    SCReturnInt(TM_ECODE_OK);
}
#endif /* LIBPCAP_VERSION_MAJOR */

/**
 * \brief This function prints stats to the screen at exit.
 * \param tv pointer to ThreadVars
 * \param data pointer that gets cast into PcapThreadVars for ptv
 */
void ReceivePcapThreadExitStats(ThreadVars *tv, void *data) {
    SCEnter();
    PcapThreadVars *ptv = (PcapThreadVars *)data;
    struct pcap_stat pcap_s;

    if (pcap_stats(ptv->pcap_handle, &pcap_s) < 0) {
        SCLogError(SC_ERR_STAT,"(%s) Failed to get pcap_stats: %s", tv->name, pcap_geterr(ptv->pcap_handle));
        SCLogInfo("(%s) Packets %" PRIu32 ", bytes %" PRIu64 "", tv->name, ptv->pkts, ptv->bytes);

        return;
    } else {
        SCLogInfo("(%s) Packets %" PRIu32 ", bytes %" PRIu64 "", tv->name, ptv->pkts, ptv->bytes);

        /* these numbers are not entirely accurate as ps_recv contains packets that are still waiting to be processed at exit.
         * ps_drop only contains packets dropped by the driver and not any packets dropped by the interface.
         * Additionally see http://tracker.icir.org/bro/ticket/18 */

        SCLogInfo("(%s) Pcap Total:%" PRIu64 " Recv:%" PRIu64 " Drop:%" PRIu64 " (%02.1f%%).", tv->name,
        (uint64_t)pcap_s.ps_recv + (uint64_t)pcap_s.ps_drop, (uint64_t)pcap_s.ps_recv,
        (uint64_t)pcap_s.ps_drop, ((float)pcap_s.ps_drop/(float)(pcap_s.ps_drop + pcap_s.ps_recv))*100);

        return;
    }
}

/**
 * \brief DeInit function closes pcap_handle at exit.
 * \param tv pointer to ThreadVars
 * \param data pointer that gets cast into PcapThreadVars for ptv
 */
TmEcode ReceivePcapThreadDeinit(ThreadVars *tv, void *data) {
    PcapThreadVars *ptv = (PcapThreadVars *)data;

    pcap_close(ptv->pcap_handle);
    SCReturnInt(TM_ECODE_OK);
}

/**
 * \brief This function passes off to link type decoders.
 *
 * DecodePcap reads packets from the PacketQueue and passes
 * them off to the proper link type decoder.
 *
 * \param t pointer to ThreadVars
 * \param p pointer to the current packet
 * \param data pointer that gets cast into PcapThreadVars for ptv
 * \param pq pointer to the current PacketQueue
 */
TmEcode DecodePcap(ThreadVars *tv, Packet *p, void *data, PacketQueue *pq, PacketQueue *postpq)
{
    SCEnter();
    DecodeThreadVars *dtv = (DecodeThreadVars *)data;

    /* update counters */
    SCPerfCounterIncr(dtv->counter_pkts, tv->sc_perf_pca);
    SCPerfCounterIncr(dtv->counter_pkts_per_sec, tv->sc_perf_pca);

    SCPerfCounterAddUI64(dtv->counter_bytes, tv->sc_perf_pca, p->pktlen);
    SCPerfCounterAddDouble(dtv->counter_bytes_per_sec, tv->sc_perf_pca, p->pktlen);
    SCPerfCounterAddDouble(dtv->counter_mbit_per_sec, tv->sc_perf_pca,
                           (p->pktlen * 8)/1000000.0);

    SCPerfCounterAddUI64(dtv->counter_avg_pkt_size, tv->sc_perf_pca, p->pktlen);
    SCPerfCounterSetUI64(dtv->counter_max_pkt_size, tv->sc_perf_pca, p->pktlen);

    /* call the decoder */
    switch(p->datalink) {
        case LINKTYPE_LINUX_SLL:
            DecodeSll(tv, dtv, p, p->pkt, p->pktlen, pq);
            break;
        case LINKTYPE_ETHERNET:
            DecodeEthernet(tv, dtv, p,p->pkt, p->pktlen, pq);
            break;
        case LINKTYPE_PPP:
            DecodePPP(tv, dtv, p, p->pkt, p->pktlen, pq);
            break;
        case LINKTYPE_RAW:
            DecodeRaw(tv, dtv, p, p->pkt, p->pktlen, pq);
            break;
        default:
            SCLogError(SC_ERR_DATALINK_UNIMPLEMENTED, "Error: datalink type %" PRId32 " not yet supported in module DecodePcap", p->datalink);
            break;
    }

    SCReturnInt(TM_ECODE_OK);
}

TmEcode DecodePcapThreadInit(ThreadVars *tv, void *initdata, void **data)
{
    SCEnter();
    DecodeThreadVars *dtv = NULL;

    dtv = DecodeThreadVarsAlloc();

    if (dtv == NULL)
        SCReturnInt(TM_ECODE_FAILED);

    DecodeRegisterPerfCounters(dtv, tv);

    *data = (void *)dtv;

    SCReturnInt(TM_ECODE_OK);
}

void PcapTranslateIPToDevice(char *pcap_dev, size_t len)
{
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_if_t *alldevsp = NULL;
	pcap_if_t *devsp = NULL;

	struct addrinfo aiHints;
	struct addrinfo *aiList = NULL;
	int retVal = 0;

	memset(&aiHints, 0, sizeof(aiHints));
	aiHints.ai_family = AF_UNSPEC;
	aiHints.ai_flags = AI_NUMERICHOST;

	/* try to translate IP */
	if ((retVal = getaddrinfo(pcap_dev, NULL, &aiHints, &aiList)) != 0) {
		return;
	}

	if (pcap_findalldevs(&alldevsp, errbuf)) {
		freeaddrinfo(aiList);
		return;
	}

	for (devsp = alldevsp; devsp ; devsp = devsp->next) {
		pcap_addr_t *ip = NULL;

		for (ip = devsp->addresses; ip ; ip = ip->next) {

			if (aiList->ai_family != ip->addr->sa_family) {
				continue;
			}

			if (ip->addr->sa_family == AF_INET) {
				if (memcmp(&((struct sockaddr_in*)aiList->ai_addr)->sin_addr, &((struct sockaddr_in*)ip->addr)->sin_addr, sizeof(struct in_addr))) {
					continue;
				}
			} else if (ip->addr->sa_family == AF_INET6) {
				if (memcmp(&((struct sockaddr_in6*)aiList->ai_addr)->sin6_addr, &((struct sockaddr_in6*)ip->addr)->sin6_addr, sizeof(struct in6_addr))) {
					continue;
				}
			} else {
				continue;
			}

			freeaddrinfo(aiList);

			memset(pcap_dev, 0, len);
			strlcpy(pcap_dev, devsp->name, len);

			pcap_freealldevs(alldevsp);
			return;
		}
	}

	freeaddrinfo(aiList);

	pcap_freealldevs(alldevsp);
}

/* eof */

