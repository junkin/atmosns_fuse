#include "params.h"
#include <fuse.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "log.h"

FILE *log_open()
{
    FILE *logfile;
    //This should be configurable.  and disableable
    logfile = fopen("atmos_fuse.log", "w");
    if (logfile == NULL) {
	exit(EXIT_FAILURE);
    }
    setvbuf(logfile, NULL, _IOLBF, 0);
    return logfile;
}

void log_msg(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    if(LOG_ENABLED)
	vfprintf(ATMOS_DATA->logfile, format, ap);
}
    
