#ifndef _PARAMS_H_
#define _PARAMS_H_
#define FUSE_USE_VERSION 26

#include <limits.h>
#include <stdio.h>
#include "transport.h"
#include <libmemcached/memcached.h>

static const int OBJECTID_SIZE = 44;

#define ATMOS_DATA ((struct atmos_state *) fuse_get_context()->private_data)
typedef struct FS_LIST {
  int fd;
  char objectid[44];
  void *next;
} fd_list;

struct atmos_state {
    FILE *logfile;
    char *rootdir;
    fd_list *head;
    memcached_st *attr_cache;
    memcached_st *block_cache;
    credentials *c;
};


#endif
