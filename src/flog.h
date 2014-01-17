#ifndef _LOG_H_
#define _LOG_H_

#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#define STR(x) #x
#define TOSTR(x) STR(x)

#ifdef NDEBUG
  
  #define LOG_DEBUG(format, ...) (void)0

#else

  #define LOG_DEBUG(format, ...) do {\
                          time_t now = time(NULL);\
                          char timestr[20];\
                          strftime(timestr, 20, "%F %T", localtime(&now));\
                          fprintf(stderr, "\033[32m[%s DEBUG] \033[0m" format "\n", timestr, ##__VA_ARGS__);}\
                          while(0) 

#endif

#define LOG_INFO(format, ...) do {\
                          time_t now = time(NULL);\
                          char timestr[20];\
                          strftime(timestr, 20, "%F %T", localtime(&now));\
                          fprintf(stderr, "\033[32m[%s INFO] \033[0m" format "\n", timestr, ##__VA_ARGS__);}\
                          while(0)

#define LOG_WARN(format, ...) do {\
                          time_t now = time(NULL);\
                          char timestr[20];\
                          strftime(timestr, 20, "%F %T", localtime(&now));\
                          fprintf(stderr, "\033[35m[%s WARN] \033[0m" format " %s:%s\n", timestr, ##__VA_ARGS__, __FILE__, TOSTR(__LINE__));}\
                          while(0)


#define LOG_ERROR(format, ...) do {\
                          time_t now = time(NULL);\
                          char timestr[20];\
                          strftime(timestr, 20, "%F %T", localtime(&now));\
                          fprintf(stderr, "\033[31m[%s ERROR] \033[0m" format " %s:%s\n", timestr, ##__VA_ARGS__, __FILE__, TOSTR(__LINE__));exit(1);}\
                          while(0)


#endif