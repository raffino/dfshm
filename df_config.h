#ifndef _DF_CONFIG_H_
#define _DF_CONFIG_H_

#define HAVE_MMAP

#define HAVE_SYSV

#define HAVE_POSIX_SHM

#define CACHE_LINE_SIZE 64


#define PAGE_SIZE 4096


/* small message threshold (1KB) */
#define DF_SHM_SMALL_MSG_THRESHOLD 1024     

/* max number of slots in a queue */
#define DF_SHM_QUEUE_LENGTH 8        

#endif

