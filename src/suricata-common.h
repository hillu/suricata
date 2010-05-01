/* Copyright (c) 2008 Victor Julien <victor@inliniac.net> */

/**
 * Common includes, etc.
 */

#ifndef __SURICATA_COMMON_H__
#define __SURICATA_COMMON_H__

#define TRUE   1
#define FALSE  0

#define _GNU_SOURCE
#define __USE_GNU

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <limits.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>

#if HAVE_SYS_SYSCALL_H
#include <sys/syscall.h>
#endif

#if HAVE_SYSCALL_H
#include <syscall.h>
#endif

#include <sys/types.h> /* for gettid(2) */
#include <sched.h>     /* for sched_setaffinity(2) */

#include <pcre.h>

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#else
#include "win32-syslog.h"
#endif /* HAVE_SYSLOG_H */

#ifdef OS_WIN32
#include "win32-misc.h"
#endif /* OS_WIN32 */
#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/time.h>

#if HAVE_POLL_H
#include <poll.h>
#endif

#if HAVE_SYS_SIGNAL_H
#include <sys/signal.h>
#endif

#if HAVE_SIGNAL_H
#include <signal.h>
#endif

#include <sys/types.h>

#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#include <sys/stat.h>

#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#if HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_WINDOWS_H
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#include <windows.h>
#endif

#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif

#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif

#include <assert.h>
#define BUG_ON(x) assert(!(x))

#include <htp/htp.h>
#include "threads.h"
#include "util-debug.h"
#include "util-error.h"
#include "util-mem.h"


/** type for the internal signature id. Since it's used in the matching engine
 *  extensively keeping this as small as possible reduces the overall memory
 *  footprint of the engine. Set to uint32_t if the engine needs to support
 *  more than 64k sigs. */
#define SigIntId uint16_t
//#define SigIntId uint32_t

size_t strlcat(char *, const char *src, size_t siz);
size_t strlcpy(char *dst, const char *src, size_t siz);

#endif /* __SURICATA_COMMON_H__ */

