#ifndef _FAKIO_UTILS_H_
#define _FAKIO_UTILS_H_


#define LOG_DEBUG 0
#define LOG_INFO 1
#define LOG_WARNING 2
#define LOG_ERROR 3

void fakio_log(int level, const char *fmt, ...);

/* for debug */
#ifdef NDEBUG
  #define LOG_FOR_DEBUG(F, ...) (void)0
#else
  #define LOG_FOR_DEBUG(F, ...) fakio_log(LOG_DEBUG, F, ##__VA_ARGS__)
#endif

#endif
