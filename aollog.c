#include "params.h"
#include <fuse.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "aollog.h"

#define LOG_ENABLED 1

#define OFF 0
#define NORMAL 1
#define DEBUG 2
#define UBERDEBUG 3


static int aol_log_level = UBERDEBUG;

FILE *log_open()
{
    FILE *logfile;
    //This should be configurable.  and disableable
    logfile = fopen("atmos_fuse.log", "w");
    if (NULL == logfile) {
	exit(EXIT_FAILURE);
    }
    setvbuf(logfile, NULL, _IOLBF, 0);
    return logfile;
}
    
void log_msg( const int level,const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    if(LOG_ENABLED && level <= aol_log_level) 
	vfprintf(ATMOS_DATA->logfile, format, ap);
   
}
 

void log_normal(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    log_msg(NORMAL,format, ap);    
}
void log_debug(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    log_msg(DEBUG,format, ap);    
}
void log_uberdebug(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    log_msg(UBERDEBUG,format, ap);    
}

void setup_log(int loglevel) {
    aol_log_level = loglevel;
}
