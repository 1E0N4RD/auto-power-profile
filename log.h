#ifndef LOG_H
#define LOG_H

#define info(...) _log("INFO", __FILE__, __LINE__, __VA_ARGS__)
#define warn(...) _log("WARNING", __FILE__, __LINE__, __VA_ARGS__)
#define error(...) _log("ERROR", __FILE__, __LINE__, __VA_ARGS__)

#define log(...) _log("", __FILE__, __LINE__, __VA_ARGS__)

void __attribute__((format(printf, 4, 5))) _log(const char *restrict prefix,
                                                const char *restrict file,
                                                int line,
                                                const char *restrict fmt, ...);

#endif
