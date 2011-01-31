/*
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/xattr.h>


#include "params.h"

#include <fuse.h>

#include "log.h"
#include "atmos_rest.h"
#include "transport.h"
#define _XOPEN_SOURCE /* glibc2 needs this */
#include <time.h>
#include <libmemcached/memcached.h>


static mode_t root_mode = 0;
#define MODE_STRING "MODE_STRING"
#define DEV_STRING "DEV_STRING"
#define UID_STRING "UID_STRING"
#define GUID_STRING "GUID_STRING"

// Report errors to logfile and give -errno to caller

int atmos_error(char *str)
{
    int ret = -errno;
    
    log_normal("    ERROR %s: %s\n", str, strerror(errno));
    
    return ret;
}

void atmos_fullpath(char fpath[PATH_MAX], const char *path)

{
    strcpy(fpath, ATMOS_DATA->rootdir);
    strncat(fpath, path, PATH_MAX); 

    log_normal("    atmos_fullpath:  rootdir = \"%s\", path = \"%s\", fpath = \"%s\"\n",
	    ATMOS_DATA->rootdir, path, fpath);
}



int atmos_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;

    ws_result wsr;
    system_meta sm;
    user_meta *um=NULL;
    char fpath[PATH_MAX];

  
    memset(statbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
	log_normal("Found root short circuiting\n");
	statbuf->st_nlink = 1; // see fuse faq
	statbuf->st_mode = root_mode | S_IFDIR;
	return 0;
    }
    statbuf->st_mode = S_IFREG | 0444;
    sprintf(fpath, "%s%s",ATMOS_DATA->rootdir, path);
    //Check cache!
  
  
#ifdef USE_MEMCACHED
    memcached_return_t error;
    char *cached_stat = memcached_get(ATMOS_DATA->attr_cache, path, strlen(path), &length, &flags, &error);
    if(cached_stat) {
	wsr.return_code = 200;
	log_normal("Cache hit for %s's attrbuf\n", path);
	memcpy(&sm, cached_stat, length);
    
    }  else { 
#endif
    //postdata pd;
    //memset(&pd, 0, sizeof(postdata));
    list_ns(ATMOS_DATA->c, fpath, NULL,0,&wsr);
    memset(&sm, 0, sizeof(sm));
    parse_headers(&wsr, &sm, &um);
    
#ifdef USE_MEMCACHED
    if(wsr.return_code == 200) memcached_set(ATMOS_DATA->attr_cache, path, strlen(path), (const char*)&sm, sizeof(sm), 0, flags);
#endif
    
    if(200 != wsr.return_code) {
	log_normal("list_ns failed with %d\n", wsr.return_code);
	retstat = -ENOENT;
	result_deinit(&wsr);      
	return retstat;
    }
    result_deinit(&wsr);

    while(um != NULL) {
	user_meta *t = um->next;
	if (strcmp(um->key, MODE_STRING) == 0) {
	    statbuf->st_mode = atoi(um->value);
	} else if (strcmp(um->key, DEV_STRING) == 0) {
	    statbuf->st_dev = atoi(um->value);
	} else if (strcmp(um->key, UID_STRING) == 0) {
	    statbuf->st_uid = atoi(um->value);
	} else if (strcmp(um->key, GUID_STRING) == 0) {
	    statbuf->st_gid = atoi(um->value);
	}
	free(um);
	um=t;
    }
  

    retstat = 0;
    statbuf->st_size = sm.size;
    statbuf->st_blocks = sm.size/512;

    statbuf->st_ino = 0;
    statbuf->st_nlink = 1;//sm.nlink; //needs to be at least 1? FIXME
    statbuf->st_rdev = 0;

    struct tm atime;
    struct tm ctime;
    struct tm mtime;
    memset(&atime, 0, sizeof(struct tm));
    memset(&ctime, 0, sizeof(struct tm));
    memset(&mtime, 0, sizeof(struct tm));
    strptime(sm.atime, "%Y-%m-%dT%H:%M:%SZ", &atime);
    strptime(sm.ctime, "%Y-%m-%dT%H:%M:%SZ", &atime);
    strptime(sm.mtime, "%Y-%m-%dT%H:%M:%SZ", &mtime);
    statbuf->st_atime = mktime(&atime);
    statbuf->st_ctime = mktime(&ctime);
    statbuf->st_mtime = mktime(&mtime);

    if(0==strcmp(sm.type, "directory")) {
	statbuf->st_mode = S_IFDIR | 0755;
    } else {
	statbuf->st_mode = S_IFREG | 0777;
    }
  
    return retstat;
}

/** Read the target of a symbolic link
 *
 * The buffer should be filled with a null terminated string.  The
 * buffer size argument includes the space for the terminating
 * null character.  If the linkname is too long to fit in the
 * buffer, it should be truncated.  The return value should be 0
 * for success.
 */

//For atmos a symbolic link is just another name for an existing file....
//symbolic links will then by the link with the target in the file.

int atmos_readlink(const char *path, char *link, size_t size)
{
    int retstat = 0;
    log_normal("atmos_readlink(path=\"%s\", link=\"%s\", size=%d)\n",
	    path, link, size);
    ws_result wsr;
    char fpath[PATH_MAX];
    sprintf(fpath, "%s%s",ATMOS_DATA->rootdir, path);

    log_normal("\natmos_readlink(path=\"%s\"\n", path);
    list_ns(ATMOS_DATA->c, fpath,NULL, 0,&wsr);
    result_deinit(&wsr);
    //retstat = lstat(fpath, statbuf);

    if(200 == wsr.return_code && wsr.response_body) {
	retstat = 0;
	strncpy(link, wsr.response_body, size);
    } else {
	retstat = -1;
    }

    if (retstat < 0)
	retstat = atmos_error("atmos_readlink readlink");
    
    return 0;
}

int add_user_meta_to_file(char *fpath, int mode, int dev) 
{
	char tmp[256];
	ws_result mwsr;
	int retstat = 0;
	bzero(tmp,256);
	sprintf(tmp,"%d",(int)mode);
	user_meta *um = new_user_meta(MODE_STRING, tmp, 0);
	bzero(tmp,256);
	sprintf(tmp,"%d",(int)dev);
	add_user_meta(um, DEV_STRING, tmp, 0);
	bzero(tmp,256);
	sprintf(tmp,"%d",getgid());
	add_user_meta(um, GUID_STRING, tmp,0);
	bzero(tmp,256);
	sprintf(tmp,"%d",getuid());
	add_user_meta(um, UID_STRING, tmp,0);

	user_meta_ns(ATMOS_DATA->c, fpath, NULL, um, &mwsr);    
	log_normal("post metadata file status %d", mwsr.return_code);	
	
	retstat = mwsr.return_code;
	result_deinit(&mwsr);
	return retstat;
}

/** Create a file node
 *
 * There is no create() operation, mknod() will be called for
 * creation of all non-directory, non-symlink nodes.
 */

int atmos_mknod(const char *path, mode_t mode, dev_t dev)
{
    int retstat = 0;
    ws_result wsr;
    struct fuse_context * fc;
    char fpath[PATH_MAX];
    log_normal("\natmos_mknod(path=\"%s\", mode=0%3o, dev=%lld)\n",
	    path, mode, dev);
  
    sprintf(fpath, "%s%s",ATMOS_DATA->rootdir, path);
    fc = fuse_get_context();    
    create_ns(ATMOS_DATA->c, fpath, NULL, NULL, NULL, &wsr);
    log_normal("atmos_mknod\t %d \n", wsr.return_code);
    if(201 ==  wsr.return_code ) {
	retstat = 0;
    } else {
	retstat = -1;
    }
    result_deinit(&wsr);
  
    if(retstat !=-1)   {
	log_normal("pre emetadata file status\n");
	int rval = 0;
	while(rval = add_user_meta_to_file(fpath,(int)mode, (int)dev) == 404) {
	    log_normal("sleeping since we got a %d", rval);
	    sleep(2);
	}
	

    }
    return retstat;
}

/** Create a directory */
int atmos_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    ws_result wsr;
    struct fuse_context * fc;
    char fpath[PATH_MAX];
    sprintf(fpath, "%s%s/",ATMOS_DATA->rootdir, path);
    fc = fuse_get_context();    
    
    log_normal("\natmos_mkdir(path=\"%s\", mode=0%3o)\n", fpath, mode);
    create_ns(ATMOS_DATA->c, fpath, NULL, NULL, NULL, &wsr);
    log_normal("%s\t\n", wsr.response_body);
    if(201 ==  wsr.return_code ) {
	retstat = 0;
    } else {
	retstat = -1;
    }
    result_deinit(&wsr);
    return retstat;
}

/** Remove a file */
int atmos_unlink(const char *path)
{
    int retstat = 0;
    ws_result wsr;
    char fpath[PATH_MAX];
    sprintf(fpath, "%s%s",ATMOS_DATA->rootdir, path);


    log_normal("atmos_unlink(path=\"%s\")\n", path);
    delete_ns(ATMOS_DATA->c, fpath, &wsr);
    //log_normal("%s\t\n", wsr.response_body);
    if(204 ==  wsr.return_code ) {
	retstat = 0;
    } else {
	retstat = -1;
    }
    result_deinit(&wsr);
    return retstat;
}


//Atmos doesnt have recursive deletes...
//FIXME
int atmos_rmdir(const char *path)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    ws_result wsr;

    log_normal("atmos_rmdir(path=\"%s\")\n",
	    path);
    atmos_fullpath(fpath, path);

    delete_ns(ATMOS_DATA->c, fpath, &wsr);
    
    log_normal("%s\t\n", wsr.response_body);
    if(204 ==  wsr.return_code ) {
	retstat = 0;
    } else {
	retstat = -1;
    }
    result_deinit(&wsr);    
    return retstat;
}

/** Create a symbolic link */
int atmos_symlink(const char *path, const char *link)
{
    int retstat = 0;
    ws_result wsr;
    char fpath[PATH_MAX];
    sprintf(fpath, "%s%s",ATMOS_DATA->rootdir,link);

    log_normal("\natmos_symlink(path=\"%s\", link=\"%s\")\n", path, link);

    create_ns(ATMOS_DATA->c, fpath, NULL, NULL, NULL, &wsr);
    log_normal("%s\t\n", wsr.response_body);
    //FIXME also needs metadata symlink=true
    if(201 ==  wsr.return_code ) {
	postdata *pd = malloc(sizeof(postdata));
	memset(pd, 0, sizeof(postdata));
	retstat = 0;
	result_deinit(&wsr);
	pd->data = malloc(strlen(link)+1);
	strcpy(pd->data, link);
	pd->body_size = strlen(link);
	update_ns(ATMOS_DATA->c, fpath, NULL, NULL, pd,NULL, &wsr);
	free (pd->data);
	free (pd);
	if (200 == wsr.return_code) {
	    retstat = 0;
	} else {
	    retstat = -1;
	}
	result_deinit(&wsr);
    } else {
	retstat = -1;
    }
    
    return retstat;
}

/** Rename a file */

int atmos_rename(const char *path, const char *newpath)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    char fnewpath[PATH_MAX];
    ws_result wsr;
    
    log_normal("\natmos_rename(fpath=\"%s\", newpath=\"%s\")\n",
	    path, newpath);
    atmos_fullpath(fpath, path);
    atmos_fullpath(fnewpath, newpath);
    
    rename_ns(ATMOS_DATA->c, fpath,fnewpath,1, &wsr);
    if(wsr.return_code >=200 && wsr.return_code < 300) 
	retstat = 0;
    else
	retstat = -1;
    return retstat;
}

/** Create a hard link to a file */
int atmos_link(const char *path, const char *newpath)
{
    int retstat = 0;
    char fpath[PATH_MAX], fnewpath[PATH_MAX];
    
    log_normal("\natmos_link(path=\"%s\", newpath=\"%s\")\n",
	    path, newpath);
    atmos_fullpath(fpath, path);
    atmos_fullpath(fnewpath, newpath);
    
    retstat = link(fpath, fnewpath);
    if (retstat < 0)
	retstat = atmos_error("atmos_link link");
    
    return retstat;
}

/** Change the permission bits of a file */
int atmos_chmod(const char *path, mode_t mode)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    ws_result wsr;
    user_meta meta;

    log_normal("\natmos_chmod(fpath=\"%s\", mode=0%03o)\n",
	    path, mode);
    atmos_fullpath(fpath, path);
  
    //Set the user meta data mode_t=mode

    memset(&meta, 0, sizeof(user_meta));
    strcpy(meta.key, "mode_t");
    memcpy((void*)&meta.value, (void*)&mode, sizeof(mode_t));
    update_ns(ATMOS_DATA->c, fpath, NULL, NULL, NULL,&meta, &wsr);
    result_deinit(&wsr);

    if (retstat < 0)
	retstat = atmos_error("atmos_chmod chmod");
    
    return retstat;
}

/** Change the owner and group of a file */
int atmos_chown(const char *path, uid_t uid, gid_t gid)
  
{
    int retstat = 0;
    char fpath[PATH_MAX];
    user_meta meta_uid;
    user_meta meta_gid;
    ws_result wsr;

  
    log_normal("\natmos_chown(path=\"%s\", uid=%d, gid=%d)\n", path, uid, gid);
    atmos_fullpath(fpath, path);

    memset(&meta_uid, 0, sizeof(user_meta));
    strcpy(meta_uid.key, "uid");

    memcpy((void*)&meta_uid.value, (void*)&uid, sizeof(uid_t));
    memset(&meta_gid, 0, sizeof(user_meta));
    strcpy(meta_gid.key, "gid");
    memcpy((void*)&meta_gid.value, (void*)&gid, sizeof(gid_t));

    meta_uid.next=&meta_gid;
    update_ns(ATMOS_DATA->c, fpath, NULL, NULL, NULL,&meta_uid, &wsr);
    result_deinit(&wsr);

    if (retstat < 0)
	retstat = atmos_error("atmos_chown chown");
    
    return retstat;
}

/** Change the size of a file */
int atmos_truncate(const char *path, off_t newsize)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_normal("\natmos_truncate(path=\"%s\", newsize=%lld)\n",
	    path, newsize);
    atmos_fullpath(fpath, path);
    if(newsize == 0) {
	ws_result result;
	//update_ns(ATMOS_DATA->c, fpath, NULL,NULL, NULL, NULL, &result);    
	delete_ns(ATMOS_DATA->c, fpath,&result);
	log_normal("%s\t\n", result.response_body);
	if(204 ==  result.return_code ) {
	    ws_result wsr;
	    retstat = 0;
	    create_ns(ATMOS_DATA->c, fpath, NULL, NULL, NULL, &wsr);
	    result_deinit(&wsr);
	} else {
	    retstat = -1;
	}

	result_deinit(&result);
    }
    //retstat = truncate(fpath, newsize);
    if (retstat < 0)
	atmos_error("atmos_truncate truncate");
    
    return retstat;
}

/** Change the access and/or modification times of a file */
int atmos_utime(const char *path, struct utimbuf *ubuf)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_normal("\natmos_utime(path=\"%s\", ubuf=0x%08x)\n",path, ubuf);
    atmos_fullpath(fpath, path);
    //FIXME  how do you change the times in atmos??
    //retstat = utime(fpath, ubuf);
    if (retstat < 0)
	retstat = atmos_error("atmos_utime utime");
    
    return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int atmos_open(const char *path, struct fuse_file_info *fi)
{
    //int retstat = 0;
    char fpath[PATH_MAX];
    
    log_normal("\natmos_open(path\"%s\", fi=0x%08x)\n",
	    path, fi);
    atmos_fullpath(fpath, path);
    
    /*
      fd = open(fpath, fi->flags);
      if (fd < 0)
      retstat = atmos_error("atmos_open open");
    
      fi->fh = fd;
      log_fi(fi);
    */

    return 0;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.

 *
 * Changed in version 2.2
 */
int atmos_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    log_normal("\nEntering atmos_Read\n");
    int retstat = 0;
    ws_result wsr;
    char fpath[PATH_MAX];
    postdata pd;
    sprintf(fpath, "%s%s",ATMOS_DATA->rootdir, path);
    pd.body_size=size;
    pd.offset=offset;
    pd.data=NULL;
    log_normal("\natmos_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n", path, buf, size, offset, fi);
    struct stat statbuf;
    atmos_getattr(path, &statbuf);
    if(statbuf.st_size ==0)
	return 0;
    list_ns(ATMOS_DATA->c, fpath,&pd, 0,&wsr);
    log_normal("\natmos_read result %d\tsized: %d\n", wsr.return_code, wsr.body_size);
    retstat = (wsr.body_size > size) ? size: wsr.body_size;

    if(wsr.return_code >= 200 && wsr.return_code < 300) {
	memcpy(buf,wsr.response_body,retstat);
	char *str = (char*)malloc(retstat+1);
	memcpy(str,wsr.response_body,retstat);
	log_normal("\natmos_rest %d\t%s\n", retstat,str);
    } else {
	log_normal("\natmos_rest read failed with %d\t%s\n", retstat,wsr.response_body);
	retstat = 0;
    }

    if (retstat < 0)
	retstat = atmos_error("atmos_read read");

    result_deinit(&wsr);    
    log_normal("returning %d requested %d\n",retstat, size);
    free(pd.data);
  
    return retstat;

}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int atmos_write(const char *path, const char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
    int retstat = 0;
    postdata *pd = malloc(sizeof(postdata));
    ws_result result;
    char fpath[PATH_MAX];
    sprintf(fpath, "%s%s",ATMOS_DATA->rootdir, path);

    log_normal("\natmos_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n", path, buf, size, offset, fi);

    pd->data = (char*)buf;//memcpy(pd->data, buf, size);
    pd->offset = offset;
    pd->body_size = size;

    update_ns(ATMOS_DATA->c, fpath, NULL,NULL, pd, NULL, &result);

    if(result.return_code >= 200 && result.return_code  < 300) {
	retstat = size;
    } else if(result.return_code == 400) {
	//offset is too big for the current object - so we pad the object with zeros from its current size ot the start of the new range.
	struct stat statbuf;
	atmos_getattr(path, &statbuf);
	int fillin_size = 0;
	fillin_size = offset-statbuf.st_size;
	log_normal("file is %d, starting at %d fill to %d\n", statbuf.st_size, offset, fillin_size);
	char *zeros = (char*)malloc(fillin_size);
	memset(zeros,0, fillin_size);
	atmos_write(path, zeros, offset-statbuf.st_size, statbuf.st_size, fi);
	free(zeros);
	atmos_write(path, buf, size, offset, fi);
   }

    //retstat = pwrite(fi->fh, buf, size, offset);
    free(pd);
    result_deinit(&result);
    
    log_normal("wrote %d to %s restful rcode =%d\n", retstat, fpath, result.return_code);

    return retstat;
}

/** Get file system statistics
 *
 * The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 *
 * Replaced 'struct statfs' parameter with 'struct statvfs' in
 * version 2.5
 */
int atmos_statfs(const char *path, struct statvfs *statv)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_normal("\natmos_statfs(path=\"%s\", statv=0x%08x)\n",
	    path, statv);
    atmos_fullpath(fpath, path);
    
    // get stats for underlying filesystem
    /*  retstat = statvfs(fpath, statv);
	if (retstat < 0)
	retstat = atmos_error("atmos_statfs statvfs");
    
	log_statvfs(statv);
    */
    return retstat;
}

/** Possibly flush cached data
 *
 * BIG NOTE: This is not equivalent to fsync().  It's not a
 * request to sync dirty data.
 *
 * Flush is called on each close() of a file descriptor.  So if a
 * filesystem wants to return write errors in close() and the file
 * has cached dirty data, this is a good place to write back data
 * and return any errors.  Since many applications ignore close()
 * errors this is not always useful.
 *
 * NOTE: The flush() method may be called more than once for each
 * open().  This happens if more than one file descriptor refers
 * to an opened file due to dup(), dup2() or fork() calls.  It is
 * not possible to determine if a flush is final, so each flush
 * should be treated equally.  Multiple write-flush sequences are
 * relatively rare, so this shouldn't be a problem.
 *
 * Filesystems shouldn't assume that flush will always be called
 * after some writes, or that if will be called at all.
 *
 * Changed in version 2.2
 */
int atmos_flush(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    //log_normal("\natmos_flush(path=\"%s\", fi=0x%08x)\n", path, fi);
    // no need to get fpath on this one, since I work from fi->fh not the path
    //If I MRU writes push those here.
    //log_fi(fi);
	
    return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int atmos_release(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    //  log_normal("\natmos_release(path=\"%s\", fi=0x%08x)\n", path, fi);
    //log_fi(fi);
    
    return retstat;
}

/** Synchronize file contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data.
 *
 * Changed in version 2.2
 */
//Same for MRU
int atmos_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    log_normal("\natmos_fsync(path=\"%s\", datasync=%d, fi=0x%08x)\n",
	    path, datasync, fi);
    
    /*  if (datasync)
	retstat = fdatasync(fi->fh);
	else
	retstat = fsync(fi->fh);
    */

    if (retstat < 0)
	atmos_error("atmos_fsync fsync");
    
    return retstat;
}

/** Set extended attributes */
//Corresponds directly with atmos meta data?
int atmos_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    int retstat = 0;
    user_meta um;
    ws_result result;
    char fpath[PATH_MAX];
    sprintf(fpath, "%s%s",ATMOS_DATA->rootdir, path);

    log_normal("\natmos_setxattr(path=\"%s\", name=\"%s\", value=\"%s\", size=%d, flags=0x%08x)\n",
	    path, name, value, size, flags);


    memset(&um, 0, sizeof(user_meta));
    strcpy(um.key, name);
    strcpy(um.value, value);
    um.listable=false; //is this true?
    user_meta_ns(ATMOS_DATA->c, fpath, NULL, &um, &result);
    result_deinit(&result);
    //  retstat = lsetxattr(fpath, name, value, size, flags);
    if (retstat < 0)
	retstat = atmos_error("atmos_setxattr lsetxattr");
    
    return retstat;
}

/** Get extended attributes */
int atmos_getxattr(const char *path, const char *name, char *value, size_t size)
{
    int retstat = 0;
    char *fpath = malloc(PATH_MAX);
    
    //  sprintf(fpath, "%s%s",ATMOS_DATA->rootdir, path);
    // log_normal("atmos_getxattr for %s %s=%s\n", path, name, value);

    /*list_ns(ATMOS_DATA->c, fpath, NULL, 0,&result);      
      parse_headers(&result, &sm, &um);
      while(um != NULL) {
      user_meta *t = um->next;
      log_normal("%s=%s %d\n", um->key, um->value, um->listable);
      if(strcmp(name, um->key) == 0 ) {
      strncpy(value, um->value, size);
      }
      free(um);
      um=t;
      }
      result_deinit(&result);
    */
    //log_normal("\natmos_getxattr(path = \"%s\", name = \"%s\", value = \"%s\", size = %d)\n",path, name, value, size);

    if (retstat < 0)
	retstat = atmos_error("atmos_getxattr lgetxattr");

    free(fpath);
    return retstat;
}

/** List extended attributes */
int atmos_listxattr(const char *path, char *list, size_t size)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    char *ptr;
    
    log_normal("atmos_listxattr(path=\"%s\", list=0x%08x, size=%d)\n",
	    path, list, size
	    );
    atmos_fullpath(fpath, path);
    
    retstat = llistxattr(fpath, list, size);
    if (retstat < 0)
	retstat = atmos_error("atmos_listxattr llistxattr");
    
    log_normal("    returned attributes (length %d):\n", retstat);
    for (ptr = list; ptr < list + retstat; ptr += strlen(ptr)+1)
	log_normal("    \"%s\"\n", ptr);
    
    return retstat;
}

/** Remove extended attributes */
int atmos_removexattr(const char *path, const char *name)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_normal("\natmos_removexattr(path=\"%s\", name=\"%s\")\n",
	    path, name);
    atmos_fullpath(fpath, path);
    
    retstat = lremovexattr(fpath, name);
    if (retstat < 0)
	retstat = atmos_error("atmos_removexattr lrmovexattr");
    
    return retstat;
}

/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int atmos_opendir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    ws_result wsr;
    log_normal("\natmos_opendir(path=\"%s\", fi=0x%08x)\n",
	    path, fi);
    atmos_fullpath(fpath, path);

    list_ns(ATMOS_DATA->c, fpath, NULL,0,&wsr);
    result_deinit(&wsr);
    //dp = opendir(fpath);
    if (wsr.return_code == 200) {
	;//fi->fh = path;
    }

    
    return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int atmos_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
		  struct fuse_file_info *fi)
{
    int retstat = 0;
    //  struct dirent *de=NULL;
    ws_result wsr;    
    //user_meta *um=NULL;
    //system_meta sm;
    char fpath[PATH_MAX];
    char *direntry = NULL;

    //postdata pd;

    postdata *pd=malloc(sizeof(postdata));
    //memset(pd, 0, sizeof(postdata));
    pd->data = NULL;
    pd->body_size = 0;
    pd->bytes_remaining = 0;
    pd->offset = 0;
    pd->bytes_written = 0;
    //*/

    static const char *needle_start="<Filename>";
    static const char *needle_end="</Filename>";

    log_normal("\natmos_readdir(path=\"%s\", buf=0x%08x, filler=0x%08x, offset=%lld, fi=0x%08x)\n",
	    path, buf, filler, offset, fi);

    atmos_fullpath(fpath, path);
    list_ns(ATMOS_DATA->c, fpath,pd,0,&wsr);

    //free(pd->data);
    free(pd);

    /*  parse_headers(&wsr, &sm, &um);
	while(um != NULL) {
	user_meta *t = um->next;
	log_normal("%s=%s %d\n", um->key, um->value, um->listable);
	free(um);
	um=t;
	}*/
    
    /*log_normal("size of body: %d\ntext of body", pd.body_size);
      char *body = malloc(pd.body_size+1);
      body[pd.body_size] = '\0';
      memcpy(body, pd.data, pd.body_size);
      log_normal("dir entries\n%s", body);
      free(pd.data);
    */
    char *body = malloc(wsr.body_size+1);
    body[wsr.body_size] = '\0';
    memcpy(body, wsr.response_body, wsr.body_size);
    log_normal("dir entries\n%s", body);

    direntry = body;
    while( (direntry = strstr(direntry,needle_start)) ) {

	int size = 0;
	char filename[1024];
	char *endptr = strstr(direntry, needle_end);
	struct stat filename_stat;
	memset(&filename_stat, 0, sizeof(struct stat));
	filename_stat.st_mode = S_IFREG | 0777;
	direntry+=strlen(needle_start);
	size = endptr-direntry;
	strncpy(filename, direntry, size);
	filename[size] = '\0';
	log_normal("dir entry: %s\t%d\n", filename, size);
	filler(buf, filename, &filename_stat,0);
    }

    free(body);
    result_deinit(&wsr);    
    log_normal("calling filler with .");
    if(filler(buf, ".", NULL, 0) !=0) {
	return -ENOMEM;
    }
    log_normal("calling filler with ..");
    if(filler(buf, "..", NULL, 0) !=0  ){
	return -ENOMEM;
    }



    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int atmos_releasedir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    log_normal("\natmos_releasedir(path=\"%s\", fi=0x%08x)\n",
	    path, fi);
    
    return retstat;
}

/** Synchronize directory contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data
 *
 * Introduced in version 2.3
 */
// when exactly is this called?  when a user calls fsync and it
// happens to be a directory? ???
int atmos_fsyncdir(const char *path, int datasync, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    log_normal("\natmos_fsyncdir(path=\"%s\", datasync=%d, fi=0x%08x)\n",
	    path, datasync, fi);
    
    return retstat;
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void *atmos_init(struct fuse_conn_info *conn)
{
    
    log_normal("\natmos_init()\n");
    
    return ATMOS_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void atmos_destroy(void *userdata)
{
    log_normal("\natmos_destroy(userdata=0x%08x)\n", userdata);
}

/**
 * Check file access permissions
 *
 * This will be called for the access() system call.  If the
 * 'default_permissions' mount option is given, this method is not
 * called.
 *
 * This method is not called under Linux kernel versions 2.4.x
 *
 * Introduced in version 2.5
 */
int atmos_access(const char *path, int mask)
{
    int retstat = 0;
    char fpath[PATH_MAX];
   
    log_normal("\natmos_access(path=\"%s\", mask=0%o)\n",
	    path, mask);
    atmos_fullpath(fpath, path);
    
    //  retstat = access(fpath, mask);
    
    if (retstat < 0)
	retstat = atmos_error("atmos_access access");
    
    return retstat;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int atmos_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    //int retstat = 0;
    char fpath[PATH_MAX];
    //    int fd;
    
    log_normal("\natmos_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
	    path, mode, fi);
    atmos_fullpath(fpath, path);
    
    /*
      fd = creat(fpath, mode);
      if (fd < 0)
      retstat = atmos_error("atmos_create creat");
    
      fi->fh = fd;
    */
    //struct fuse_context * fc = fuse_get_context();


    
    return 0;
}

/**
 * Change the size of an open file
 *
 * This method is called instead of the truncate() method if the
 * truncation was invoked from an ftruncate() system call.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the truncate() method will be
 * called instead.
 *
 * Introduced in version 2.5
 */
int atmos_ftruncate(const char *path, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    log_normal("\natmos_ftruncate(path=\"%s\", offset=%lld, fi=0x%08x)\n",
	    path, offset, fi);
    
    retstat = ftruncate(fi->fh, offset);
    if (retstat < 0)
	retstat = atmos_error("atmos_ftruncate ftruncate");
    
    return retstat;
}

/**
 * Get attributes from an open file
 *
 * This method is called instead of the getattr() method if the
 * file information is available.
 *
 * Currently this is only called after the create() method if that
 * is implemented (see above).  Later it may be called for
 * invocations of fstat() too.
 *
 * Introduced in version 2.5
 */
int atmos_fgetattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi)
{

    int retstat = atmos_getattr(path, statbuf);
    /*  int retstat = 0;
    
	log_normal("\natmos_fgetattr(path=\"%s\", statbuf=0x%08x, fi=0x%08x)\n",
	path, statbuf, fi);
	log_fi(fi);
    
	retstat = fstat(fi->fh, statbuf);
	if (retstat < 0)
	retstat = atmos_error("atmos_fgetattr fstat");
    
	log_stat(statbuf);
    */
    return retstat;
}

struct fuse_operations atmos_oper = {
    .getattr = atmos_getattr,
    .readlink = atmos_readlink,
    // no .getdir -- that's deprecated
    .getdir = NULL,
    .mknod = atmos_mknod,
    .mkdir = atmos_mkdir,
    .unlink = atmos_unlink,
    .rmdir = atmos_rmdir,
    .symlink = atmos_symlink,
    .rename = atmos_rename,
    .link = atmos_link,
    .chmod = atmos_chmod,
    .chown = atmos_chown,
    .truncate = atmos_truncate,
    .utime = atmos_utime,
    .open = atmos_open,
    .read = atmos_read,
    .write = atmos_write,
    /** Just a placeholder, don't set */ // huh???
    .statfs = atmos_statfs,
    .flush = atmos_flush,
    .release = atmos_release,
    .fsync = atmos_fsync,
    .setxattr = atmos_setxattr,
    .getxattr = atmos_getxattr,
    .listxattr = atmos_listxattr,
    .removexattr = atmos_removexattr,
    .opendir = atmos_opendir,
    .readdir = atmos_readdir,
    .releasedir = atmos_releasedir,
    .fsyncdir = atmos_fsyncdir,
    .init = atmos_init,
    .destroy = atmos_destroy,
    .access = atmos_access,
    //  .create = atmos_create,
    //.ftruncate = atmos_ftruncate, 
    .fgetattr = atmos_fgetattr
};

void atmos_usage()
{
    fprintf(stderr, "usage:  atmosfs mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int i;
    int fuse_stat;
    struct atmos_state *atmos_data;
    ws_result wsr;
    //static int opt_binary= 0;
    //setup memcaceh
    atmos_data = calloc(sizeof(struct atmos_state), 1);
  
    //    atmos_data->attr_cache = memcached_create(NULL);
    
    /*    memcached_server_st *servers= memcached_servers_parse("localhost");
	  memcached_server_push(atmos_data->attr_cache, servers);
	  memcached_server_list_free(servers);
	  memcached_behavior_set(atmos_data->attr_cache, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL,
	  (uint64_t)opt_binary);
    */  

    if (atmos_data == NULL) {
	perror("main calloc");
	abort();
    }
    
    atmos_data->logfile = log_open();

    for (i = 1; (i < argc) && (argv[i][0] == '-'); i++);
    if (i == argc)
	atmos_usage();
    
    atmos_data->rootdir = "/ATMOSFUSE";

  
    for (; i < argc; i++)
	argv[i] = argv[i+1];
    argc--;

  
    static const char *user_id = "whitewater";
    static const char *key = "EIJHmj9JZSGVFQ2Hsl/scAsKm00=";
    static const char *endpoint = "10.245.35.162";  //*/
    static const char *rootdir = "/ATMOSFUSE/";
    atmos_data->c = init_ws(user_id, key, endpoint);
      
    create_ns(atmos_data->c, rootdir,NULL, NULL, NULL, &wsr);
    //  log_normal("atmos setup to receive fuse %d\n", wsr.return_code);  
    result_deinit(&wsr);
      
      
    //  log_normal("about to call fuse_main\n");
    fuse_stat = fuse_main(argc, argv, &atmos_oper, atmos_data);
    //  log_normal("fuse_main returned %d\n", fuse_stat);
    free(atmos_data->c);
    free(atmos_data);
    
    return fuse_stat;
}
