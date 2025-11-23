#include <stdarg.h>
#include <stdio.h>

void __attribute__((format(printf, 4, 5)))
do_log(const char *prefix, const char *file, int line, const char *restrict fmt,
       ...) {
    fprintf(stderr, "%s@%s: %d: ", prefix, file, line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
}
