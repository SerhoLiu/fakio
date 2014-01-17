#include "futils.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

#define MAX_LOG_LENGTH 1024

/*
 * [2014-01-17 13:57:24 INFO] -----
 */
void fakio_log(int level, const char *fmt, ...)
{
    const char *debug = "\033[32m[%F %T DEBUG] \033[0m";
    const char *info = "\033[32m[%F %T INFO] \033[0m";
    const char *warn = "\033[35m[%F %T WARN] \033[0m";
    const char *error = "\033[31m[%F %T ERROR] \033[0m";
    
    int off;
    va_list ap;
    char logmsg[MAX_LOG_LENGTH];
    struct timeval tv;
    gettimeofday(&tv,NULL);
    
    const char *timefmt = NULL;

    switch (level) {
        case LOG_DEBUG: timefmt = debug; break;
        case LOG_INFO: timefmt = info; break;
        case LOG_WARNING: timefmt = warn; break;
        case LOG_ERROR: timefmt = error; break;
        default: return;
    }

    off = strftime(logmsg, sizeof(logmsg), timefmt, localtime(&tv.tv_sec));

    va_start(ap, fmt);
    vsnprintf(logmsg+off, sizeof(logmsg)-off, fmt, ap);
    va_end(ap);

    fprintf(stderr, "%s\n", logmsg);
}
