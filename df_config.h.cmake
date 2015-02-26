#ifndef _DF_CONFIG_H_
#define _DF_CONFIG_H_

#cmakedefine HAVE_MMAP

#cmakedefine HAVE_SYSV

#cmakedefine HAVE_POSIX_SHM

#cmakedefine CACHE_LINE_SIZE @CACHE_LINE_SIZE@

#cmakedefine PAGE_SIZE @PAGE_SIZE@

/* small message threshold (1KB) */
#define DF_SHM_SMALL_MSG_THRESHOLD 1024     

/* max number of slots in a queue */
#define DF_SHM_QUEUE_LENGTH 8        

#endif

