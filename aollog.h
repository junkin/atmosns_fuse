#ifndef _AOLLOG_H_
#define _AOLLOG_H_
#include <stdio.h>



FILE *log_open(void);
void log_msg(const int loglevel,const char *format, ...);
void log_normal(const char *format, ...) ;
void log_debug(const char *format, ...) ;
void log_uberdebug(const char *format, ...) ;
void setup_log(int loglevel);


#endif
