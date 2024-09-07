#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libavutil/log.h>
#include "pktav_log.h"

void pktav_log_callback(void* ptr, int level, const char* fmt, va_list vl) {
    pid_t id;
    if (level <= av_log_get_level()) {
        id = getpid();

        time_t rawtime;
        struct tm *timeinfo;
        char buffer[80];

        time(&rawtime);
        timeinfo = gmtime(&rawtime);

        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S.000000 +0000 UTC]", timeinfo);
        fprintf(logFile, "%s Pid: %lu - ", buffer, (unsigned long) id);
        vfprintf(logFile, fmt, vl);
    }
}

void pktav_log(void *ptr, int level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    pktav_log_callback(NULL, level, fmt, args);
    va_end(args);
}