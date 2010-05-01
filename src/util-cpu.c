/**
 * Copyright (c) 2009 Open Information Security Foundation
 *
 * \author Pablo Rincon Crespo <pablo.rincon.crespo@gmail.com>
 *
 * Retrieve CPU information (configured CPUs, online CPUs)
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "util-error.h"
#include "util-debug.h"
#include "suricata-common.h"

/**
 * Ok, if they should use sysconf, check that they have the macro's
 * (syscalls) defined;
 *
 * Note: For windows it's different; Check the following:
 *      SYSTEM_INFO info;
 *      GetSystemInfo(&info);
 *      -> info.dwNumberOfProcessors;
 */
#ifdef _SC_NPROCESSORS_CONF
#define SYSCONF_NPROCESSORS_CONF_COMPAT
#endif

#ifdef _SC_NPROCESSORS_ONLN
#define SYSCONF_NPROCESSORS_ONLN_COMPAT
#endif

/* This one is available on Solaris 10 */
#ifdef _SC_NPROCESSORS_MAX
#define SYSCONF_NPROCESSORS_MAX_COMPAT
#endif

/**
 * \brief Get the number of cpus configured in the system
 * \retval 0 if the syscall is not available or we have an error;
 *           otherwise it will return the number of cpus configured
 */
uint16_t UtilCpuGetNumProcessorsConfigured() {
#ifdef SYSCONF_NPROCESSORS_CONF_COMPAT
	long nprocs = -1;
    nprocs = sysconf(_SC_NPROCESSORS_CONF);
    if (nprocs < 1) {
        SCLogError(SC_ERR_SYSCALL, "Couldn't retrieve the number of cpus "
                   "configured (%s)", strerror(errno));
        return 0;
    }

    if (nprocs > UINT16_MAX) {
        SCLogDebug("It seems that there are more than %"PRIu16" CPUs "
                   "configured on this system. You can modify util-cpu.{c,h} "
                   "to use uint32_t to support it", UINT16_MAX);
        return UINT16_MAX;
    }

    return (uint16_t)nprocs;
#elif OS_WIN32
	long nprocs = -1;
	const char* envvar = getenv("NUMBER_OF_PROCESSORS");
	nprocs = (NULL != envvar) ? atoi(envvar) : 0;
    if (nprocs < 1) {
        SCLogError(SC_ERR_SYSCALL, "Couldn't retrieve the number of cpus "
                   "configured from the NUMBER_OF_PROCESSORS environment variable");
        return 0;
    }
    return (uint16_t)nprocs;
#else
    SCLogError(SC_ERR_SYSCONF, "Couldn't retrieve the number of cpus "
               "configured, sysconf macro unavailable");
    return 0;
#endif
}

/**
 * \brief Get the number of cpus online in the system
 * \retval 0 if the syscall is not available or we have an error;
 *           otherwise it will return the number of cpus online
 */
uint16_t UtilCpuGetNumProcessorsOnline() {
#ifdef SYSCONF_NPROCESSORS_ONLN_COMPAT
    long nprocs = -1;
    nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs < 1) {
        SCLogError(SC_ERR_SYSCALL, "Couldn't retrieve the number of cpus "
                   "online (%s)", strerror(errno));
        return 0;
    }

    if (nprocs > UINT16_MAX) {
        SCLogDebug("It seems that there are more than %"PRIu16" CPUs online. "
                   "You can modify util-cpu.{c,h} to use uint32_t to "
                   "support it", UINT16_MAX);
        return UINT16_MAX;
    }

    return nprocs;
#elif OS_WIN32
	return UtilCpuGetNumProcessorsConfigured();
#else
    SCLogError(SC_ERR_SYSCONF, "Couldn't retrieve the number of cpus online, "
               "synconf macro unavailable");
    return 0;
#endif
}

/**
 * \brief Get the maximum number of cpus allowed in the system
 *        This syscall is present on Solaris, but it's not on linux
 *        or macosx. Maybe you should look at UtilCpuGetNumProcessorsConfig()
 * \retval 0 if the syscall is not available or we have an error;
 *           otherwise it will return the number of cpus allowed
 */
uint16_t UtilCpuGetNumProcessorsMax() {
#ifdef SYSCONF_NPROCESSORS_MAX_COMPAT
    long nprocs = -1;
    nprocs = sysconf(_SC_NPROCESSORS_MAX);
    if (nprocs < 1) {
        SCLogError(SC_ERR_SYSCALL, "Couldn't retrieve the maximum number of cpus "
                   "allowed by the system (%s)", strerror(errno));
        return 0;
    }

    if (nprocs > UINT16_MAX) {
        SCLogDebug("It seems that the system support more that %"PRIu16" CPUs. You "
                   "can modify util-cpu.{c,h} to use uint32_t to support it", UINT16_MAX);
        return UINT16_MAX;
    }

    return (uint16_t)nprocs;
#else
    SCLogError(SC_ERR_SYSCONF, "Couldn't retrieve the maximum number of cpus allowed by "
               "the system, synconf macro unavailable");
    return 0;
#endif
}

/**
 * \brief Print a summary of CPUs detected (configured and online)
 */
void UtilCpuPrintSummary() {
    uint16_t cpus_conf = UtilCpuGetNumProcessorsConfigured();
    uint16_t cpus_online = UtilCpuGetNumProcessorsOnline();

    SCLogInfo("CPUs Summary: ");
    if (cpus_conf > 0)
        SCLogInfo("CPUs online: %"PRIu16, cpus_conf);
    if (cpus_online > 0)
        SCLogInfo("CPUs configured %"PRIu16, cpus_online);
    if (cpus_online == 0 && cpus_conf == 0)
        SCLogInfo("Couldn't retireve any information of CPU's, please, send your operating "
                  "system info and check util-cpu.{c,h}");
}

