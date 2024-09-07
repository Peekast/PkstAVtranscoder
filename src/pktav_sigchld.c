#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "pktav_log.h"
#include "pktav_error.h"

void sigchld_handler(int signo) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            pktav_log(NULL, 0, "End process (Pid:%d) with status: %d\n", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            pktav_log(NULL, 0, "End process (Pid:%d) by a signal: %d\n", pid, WTERMSIG(status));
        }
    }
}

int set_sigchld_handler(void) {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;  
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        pktav_errno = errno;
        return -OS_ERROR;
    }
    pktav_errno = 0;
    return 0;
}

