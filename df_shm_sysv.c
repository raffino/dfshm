/*
 * System V shared memory method
 *
 */
#include "df_config.h"

#ifdef HAVE_SYSV

#include <unistd.h>
#include <stdio.h> 
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "df_shm.h"

#define DEFAULT_SYSV_SHM_FLAG 0600
#define PATH_LENGTH 100

/*
 * global method level bookkeeping data
 */
typedef struct _shm_sysv_method_data {
    int default_flag;
    char path[PATH_LENGTH];
    int token_id;
    pid_t my_pid;
} shm_sysv_method_data, *shm_sysv_method_data_t;
 
/*
 * per-region data
 */ 
typedef struct _shm_sysv_region_data {
    key_t key;
    int id;
    void *attach_addr;
} shm_sysv_region_data, *shm_sysv_region_data_t; 
 
int df_shm_method_sysv_init (void *input_data, void **method_data)
{
    // TODO: get config parameters from input_data. ignore it now.
    shm_sysv_method_data_t m_data = (shm_sysv_method_data_t) 
        malloc(sizeof(shm_sysv_method_data));
    if(!m_data) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return -1;    
    }
    m_data->default_flag = DEFAULT_SYSV_SHM_FLAG;
    m_data->my_pid = getpid();
    
    // create a per-process file with unique path used to generate unique shm keys
    int token_id = 1;
    sprintf(m_data->path, "/tmp/df_shm_sysv.%d", m_data->my_pid);
    int fd = open(m_data->path, m_data->default_flag | O_CREAT | O_RDWR);
    if(fd != -1) { // file created
        close(fd);
        fprintf(stderr, "Debug: process (%d) created path %s. %s:%d\n", 
            m_data->my_pid, m_data->path, __FILE__, __LINE__);
    }
    else {
        fprintf(stderr, "Error: process (%d) open %s failed. %s:%d\n", 
            m_data->my_pid, m_data->path, __FILE__, __LINE__);
        perror("open");
        free(m_data);
        return -1;
    }
    *method_data = m_data;
    return 0;
}

int df_shm_method_sysv_create_region (void *method_data, 
                                      size_t size, 
                                      void *starting_addr, 
                                      void **return_data, 
                                      void **attach_address
                                     )
{
    shm_sysv_method_data_t m_data = (shm_sysv_method_data_t) method_data;

    // create per-region data
    shm_sysv_region_data_t region_data = (shm_sysv_region_data_t) malloc(sizeof(shm_sysv_region_data));
    if(!region_data) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return -1;    
    }
    
    // generate a unique key
    region_data->key = ftok(m_data->path, m_data->token_id);
    if(region_data->key == (key_t) -1) {
        fprintf(stderr, "Error: calling ftok() failed: %d %s:%d\n", errno, __FILE__, __LINE__);
        free(region_data);
        return -1;
    }
    m_data->token_id ++;    
    
    // get a shared memory segment
    region_data->id = shmget(region_data->key, size, IPC_CREAT | O_EXCL | m_data->default_flag);
    if(region_data->id == -1) {
        fprintf(stderr, "Error: shmget() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        free(region_data);
        return -1;
    }    
    
    if(starting_addr != NULL && (uint64_t)starting_addr % SHMLBA) { 
        fprintf(stderr, "Warning: the starting address (%p) is not page-aligned. %s:%d\n", 
            starting_addr, __FILE__, __LINE__);
    }

    // attach the created shm segment
    void *attach_addr = shmat(region_data->id, starting_addr, 0 | SHM_RND);
    if(attach_addr == (void *) -1) {
        fprintf(stderr, "Error: shmat() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        free(region_data);
        return -1;
    }
    if(attach_addr != starting_addr) {
        fprintf(stderr, "Warning: shared memory region attached to %p instead of %p. %s:%d\n", 
            attach_addr, starting_addr, __FILE__, __LINE__);
    }
    region_data->attach_addr = attach_addr;
    *attach_address = attach_addr;
    *return_data = region_data;
    return 0;
}

/*
 * Create a shm region based on the 'name' parameter. 'name' parameter
 * specifies the key of shm region.
 */
int df_shm_method_sysv_create_named_region (void *method_data,
                                      void *name,
                                      int name_size, 
                                      size_t size,
                                      void *starting_addr,
                                      void **return_data,
                                      void **attach_address
                                     )
{
    shm_sysv_method_data_t m_data = (shm_sysv_method_data_t) method_data;

    // create per-region data
    shm_sysv_region_data_t region_data = (shm_sysv_region_data_t) malloc(sizeof(shm_sysv_region_data));
    if(!region_data) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return -1;
    }

    // generate a unique key
    region_data->key = *((key_t *)name);

    // get a shared memory segment
    region_data->id = shmget(region_data->key, size, IPC_CREAT | O_EXCL | m_data->default_flag);
    if(region_data->id == -1) {
        fprintf(stderr, "Error: shmget() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        free(region_data);
        return -1;
    }

    if(starting_addr != NULL && (uint64_t)starting_addr % SHMLBA) {
        fprintf(stderr, "Warning: the starting address (%p) is not page-aligned. %s:%d\n",
            starting_addr, __FILE__, __LINE__);
    }

    // attach the created shm segment
    void *attach_addr = shmat(region_data->id, starting_addr, 0 | SHM_RND);
    if(attach_addr == (void *) -1) {
        fprintf(stderr, "Error: shmat() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        free(region_data);
        return -1;
    }
    if(attach_addr != starting_addr) {
        fprintf(stderr, "Warning: shared memory region attached to %p instead of %p. %s:%d\n",
            attach_addr, starting_addr, __FILE__, __LINE__);
    }
    region_data->attach_addr = attach_addr;
    *attach_address = attach_addr;
    *return_data = region_data;
    return 0;
}

/*
 * the contact info of a mmap shm region has the following fields:
 * - key (sizeof(key_t) bytes)
 */
void * df_shm_method_sysv_region_contact (void *method_data, df_shm_region_t region, int *length)
{
    shm_sysv_region_data_t region_data = (shm_sysv_region_data_t) region->method_data;

    int *contact_string;
    contact_string = (int *) malloc(sizeof(int));
    if(!contact_string) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return NULL;
    }
    *contact_string = region_data->key;
    *length = sizeof(key_t);
    return contact_string;
}

int df_shm_method_sysv_destroy_region (void *method_data, df_shm_region_t region)
{
    shm_sysv_method_data_t m_data = (shm_sysv_method_data_t) method_data;    
    shm_sysv_region_data_t region_data = region->method_data;

    // detach the shm segement
    if(shmdt(region_data->attach_addr) == -1) {
        fprintf(stderr, "Error: shmdt() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        return -1;    
    }

    // mark shm segment to be removed
    if(shmctl(region_data->id, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "Error: shmctl() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        return -1;
    }
    
    free(region_data);
    return 0;
}

int df_shm_method_sysv_attach_region (void *method_data, 
                                      void *contact_info, 
                                      size_t size, 
                                      void *starting_addr, 
                                      void **return_data, 
                                      void **attach_address
                                     )
{
    shm_sysv_method_data_t m_data = (shm_sysv_method_data_t) method_data;

    // create per-region data
    shm_sysv_region_data_t region_data = (shm_sysv_region_data_t) 
        malloc(sizeof(shm_sysv_region_data));
    if(!region_data) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return -1;    
    }
    
    // TODO: get shm segment info from contact info
    region_data->key = *(key_t *) contact_info;
    
    if(starting_addr != NULL && (uint64_t)starting_addr % SHMLBA) { 
        fprintf(stderr, "Warning: the starting address (%p) is not page-aligned. %s:%d\n", 
            starting_addr, __FILE__, __LINE__);
    }

    region_data->id = shmget(region_data->key, size, m_data->default_flag);
    if(region_data->id == -1) {
        fprintf(stderr, "Error: shmget() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        free(region_data);
        return -1;
    }

    // attach the created shm segment
     void *attach_addr = shmat(region_data->id, starting_addr, 0 | SHM_RND);
    if(attach_addr == (void *) -1) {
        fprintf(stderr, "Error: shmat() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        free(region_data);
        return -1;
    }
    if(starting_addr != NULL && attach_addr != starting_addr) {
        fprintf(stderr, "Warning: shared memory region attached to %p instead of %p. %s:%d\n", 
            attach_addr, starting_addr, __FILE__, __LINE__);
    }
    region_data->attach_addr = attach_addr;
    *return_data = region_data;
    *attach_address = attach_addr;
    return 0;
}

int df_shm_method_sysv_detach_region (void *method_data, df_shm_region_t region)
{
    shm_sysv_method_data_t m_data = (shm_sysv_method_data_t) method_data;    
    shm_sysv_region_data_t region_data = region->method_data;

    if(shmdt(region_data->attach_addr) == -1) {
        fprintf(stderr, "Error: shmdt() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        return -1;    
    }
    free(region_data);
    return 0;    
}

int df_shm_method_sysv_finalize (void *method_data)
{
    shm_sysv_method_data_t m_data = (shm_sysv_method_data_t) method_data;    
    if(unlink(m_data->path) == -1) {
        fprintf(stderr, "Error: unlink() file %s returns %d. %s:%d\n", 
            m_data->path, errno, __FILE__, __LINE__);
        return -1;        
    }
    free(m_data);
    return 0;
}
  
#endif /* HAVE_SYSV */

