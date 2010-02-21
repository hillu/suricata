/**
 * Copyright (c) 2009 Open Information Security Foundation
 *
 * \file util-daemon.c
 * \author Gerardo Iglesias Galvan <iglesiasg@gmail.com>
 *
 * Daemonization process
 */

#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "suricata.h"
#include "suricata-common.h"
#include "util-daemon.h"
#include "util-debug.h"

static volatile sig_atomic_t sigflag = 0;

/**
 * \brief Signal handler used to take the parent process out of stand-by
 */
static void SignalHandlerSigusr1 (int signo) {
    sigflag = 1;
}

/**
 * \brief Tell the parent process the child is ready
 *
 * \param pid pid of the parent process to signal
 */
static void TellWaitingParent (pid_t pid) {
    kill(pid, SIGUSR1);
}

/**
 * \brief Set the parent on stand-by until the child is ready
 *
 * \param pid pid of the child process to wait
 */
static void WaitForChild (pid_t pid) {
    int status;
    SCLogDebug("Daemon: Parent waiting for child to be ready...");
    /* Wait until child signals is ready */
    while (sigflag == 0) {
        if (waitpid(pid, &status, WNOHANG)) {
            /* Check if the child is still there, otherwise the parent should exit */
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                SCLogError(SC_ERR_DAEMON, "Child died unexpectedly");
                exit(EXIT_FAILURE);
            }
        }
        /* sigsuspend(); */
        sleep(1);
    }
}

/**
 * \brief Close stdin, stdout, stderr.Redirect logging info to syslog
 *
 */
static void SetupLogging () {
    int fd0, fd1, fd2;

    SCLogInitData *sc_lid = NULL;
    SCLogOPIfaceCtx *sc_iface_ctx = NULL;

    sc_lid = SCLogAllocLogInitData();
    sc_lid->startup_message = "Daemon started";
    sc_lid->global_log_level = SC_LOG_INFO;
    sc_iface_ctx = SCLogInitOPIfaceCtx("syslog", "%l", SC_LOG_INFO, "local5");
    SCLogAppendOPIfaceCtx(sc_iface_ctx, sc_lid);

    /* Close stdin, stdout, stderr */
    close(0);
    close(1);
    close(2);

    /* Redirect stdin, stdout, stderr to /dev/null  */
    fd0 = open("/dev/null", O_RDWR);
    fd1 = dup(0);
    fd2 = dup(0);

    SCLogInitLogModule(sc_lid);
}

/**
 * \brief Check for a valid combination daemon/mode
 *
 * \param daemon daemon on or off
 * \param mode selected mode
 *
 * \retval 1 valid combination
 * \retval 0 invalid combination
 */
int CheckValidDaemonModes (int daemon, int mode) {
    if (daemon) {
        switch (mode) {
            case MODE_PCAP_FILE:
                SCLogError(SC_INVALID_RUNMODE, "ERROR: pcap offline mode cannot run as daemon");
                return 0;
            case MODE_UNITTEST:
                SCLogError(SC_INVALID_RUNMODE, "ERROR: unittests cannot run as daemon");
                return 0;
            default:
                SCLogDebug("Allowed mode");
                break;
        }
    }
    return 1;
}

/**
 * \brief Daemonize the process
 *
 */
void Daemonize (void) {
    pid_t pid, sid;

    /* Register the signal handler */
    signal(SIGUSR1, SignalHandlerSigusr1);

    /** \todo We should check if wie allow more than 1 instance
              to run simultaneously. Maybe change the behaviour
              through conf file */

    /* Creates a new process */
    pid = fork();

    if (pid < 0) {
        /* Fork error */
        SCLogError(SC_ERR_DAEMON, "Error forking the process");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        /* Child continues here */

        umask(027);

        sid = setsid();
        if (sid < 0) {
            SCLogError(SC_ERR_DAEMON, "Error creating new session");
            exit(EXIT_FAILURE);
        }

        /** \todo The daemon runs on the current directory, but later we'll want
                  to allow through the configuration file (or other means) to
                  change the running directory  */
        /* if ((chdir(DAEMON_WORKING_DIRECTORY)) < 0) {
            SCLogError(SC_ERR_DAEMON, "Error changing to working directory");
            exit(EXIT_FAILURE);
        } */

        SetupLogging();

        /* Child is ready, tell its parent */
        TellWaitingParent(getppid());

        /* Daemon is up and running */
        SCLogDebug("Daemon is running");
        return;
    }
    /* Parent continues here, waiting for child to be ready */
    SCLogDebug("Parent is waiting for child to be ready");
    WaitForChild(pid);

    /* Parent exits */
    SCLogDebug("Child is ready, parent exiting");
    exit(EXIT_SUCCESS);

}
