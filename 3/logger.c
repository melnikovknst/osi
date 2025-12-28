#include <stdarg.h>
#include <stdio.h>

#include "logger.h"
#include "config.h"

void log_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("[INFO] ", stdout);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
}

void log_err(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("[ERR] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}
