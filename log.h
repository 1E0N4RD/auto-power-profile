#ifndef LOG_H
#define LOG_H

#define info(...) do_log("INFO", __FILE__, __LINE__, __VA_ARGS__)
#define warn(...) do_log("WARNING", __FILE__, __LINE__, __VA_ARGS__)
#define error(...) do_log("ERROR", __FILE__, __LINE__, __VA_ARGS__)

void __attribute__((format(printf, 4, 5)))
do_log(const char *restrict prefix, const char *restrict file, int line,
       const char *restrict fmt, ...);

#endif
