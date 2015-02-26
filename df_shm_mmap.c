/*
 * shared memory method by mmap()-ed a common backstore file
 *
 */
#include "df_config.h"

#ifdef HAVE_MMAP

#include <unistd.h>
#include <stdio.h> 
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "df_shm.h"


#define PATH_LENGTH 100
#define DEFAULT_OPEN_MODE 0600

/*
 * global method level bookkeeping data
 */
typedef struct _shm_mmap_method_data {
    char base_path[PATH_LENGTH]; 
    pid_t my_pid;
} shm_mmap_method_data, *shm_mmap_method_data_t;
 
/*
 * per-region data
 */ 
typedef struct _shm_mmap_region_data {
    char *file_name;   
    size_t file_length;
    void *attach_addr;
    size_t mapped_length;  
} shm_mmap_region_data, *shm_mmap_region_data_t; 
 
int df_shm_method_mmap_init (void *input_data, void **method_data)
{
    // TODO: get config parameters from input_data. ignore it now.
    shm_mmap_method_data_t m_data = (shm_mmap_method_data_t) malloc(sizeof(shm_mmap_method_data));
    if(!m_data) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return -1;    
    }
    m_data->my_pid = getpid();
    
    // create base path for backstore file
    sprintf(m_data->base_path, "/tmp/df_shm_mmap.%d.XXXXXX", m_data->my_pid);
    *method_data = m_data;
    return 0;
}

int df_shm_method_mmap_create_region (void *method_data, 
                                      size_t size, 
                                      void *starting_addr, 
                                      void **return_data, 
                                      void **attach_address
                                     )
{
    shm_mmap_method_data_t m_data = (shm_mmap_method_data_t) method_data;

    // create per-region data
    shm_mmap_region_data_t region_data = (shm_mmap_region_data_t) 
        malloc(sizeof(shm_mmap_region_data));
    if(!region_data) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return -1;    
    }
    
    // atomically generate a unique file name(base_path.xxxxxx) and create the backstore file
    region_data->file_name =strdup(m_data->base_path);
    int fd = mkstemp(region_data->file_name);
    if(fd == -1) {
        fprintf(stderr, "Error: calling mkstemp() on %s failed: %d %s:%d\n", 
            region_data->file_name, errno, __FILE__, __LINE__);
        free(region_data->file_name);
        free(region_data);
        return -1;
    }
    
    // size the backstore file
    if(ftruncate(fd, size) == -1) {
        fprintf(stderr, "Error: ftruncate() file %s to size %lu failed: %d %s:%d\n", 
            region_data->file_name, size, errno, __FILE__, __LINE__);
        free(region_data->file_name);
        free(region_data);
        return -1;
    }
    region_data->file_length = size;
    
    if(starting_addr != NULL && (uint64_t)starting_addr % PAGE_SIZE) {
        fprintf(stderr, "Warning: the starting address (%p) is not page-aligned. %s:%d\n", 
            starting_addr, __FILE__, __LINE__);    
    }
    
    // map the file to local address space
    region_data->attach_addr = mmap(starting_addr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(region_data->attach_addr == NULL) {
        fprintf(stderr, "Error: mmap() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        free(region_data->file_name);
        free(region_data);
        return -1;
    }    
    region_data->mapped_length = size;
    if(region_data->attach_addr != starting_addr) {
        fprintf(stderr, "Warning: shared memory region attached to %p instead of %p. %s:%d\n", 
            region_data->attach_addr, starting_addr, __FILE__, __LINE__);
    }
    
    // close the file descrptor
    if(close(fd) == -1) {
        fprintf(stderr, "Error: close() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        munmap(region_data->attach_addr, region_data->mapped_length);
        free(region_data->file_name);
        free(region_data);
        return -1;    
    }
    
    *return_data = region_data;
    *attach_address = region_data->attach_addr;
    return 0;
}

/*
 * Create a shm region using the backstore file specified by 'name'.
 * 'name' parameter is a string specifying the path of backstore file.
 */
int df_shm_method_mmap_create_named_region (void *method_data,
                                      void *name,
                                      int name_size,
                                      size_t size,
                                      void *starting_addr,
                                      void **return_data,
                                      void **attach_address
                                     )
{
    shm_mmap_method_data_t m_data = (shm_mmap_method_data_t) method_data;

    // create per-region data
    shm_mmap_region_data_t region_data = (shm_mmap_region_data_t)
        malloc(sizeof(shm_mmap_region_data));
    if(!region_data) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return -1;
    }

    // atomically generate a unique file name(base_path.xxxxxx) and create the backstore file
    region_data->file_name =strdup((char *)name);
    int fd = open(region_data->file_name, O_RDWR | O_CREAT | O_TRUNC, DEFAULT_OPEN_MODE);
    if(fd == -1) {
        fprintf(stderr, "Error: calling open() on %s failed: %d %s:%d\n",
            region_data->file_name, errno, __FILE__, __LINE__);
        free(region_data->file_name);
        free(region_data);
        return -1;
    }

    // size the backstore file
    if(ftruncate(fd, size) == -1) {
        fprintf(stderr, "Error: ftruncate() file %s to size %lu failed: %d %s:%d\n",
            region_data->file_name, size, errno, __FILE__, __LINE__);
        free(region_data->file_name);
        free(region_data);
        return -1;
    }
    region_data->file_length = size;

    if(starting_addr != NULL && (uint64_t)starting_addr % PAGE_SIZE) {
        fprintf(stderr, "Warning: the starting address (%p) is not page-aligned. %s:%d\n",
            starting_addr, __FILE__, __LINE__);
    }

    // map the file to local address space
    region_data->attach_addr = mmap(starting_addr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(region_data->attach_addr == NULL) {
        fprintf(stderr, "Error: mmap() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        free(region_data->file_name);
        free(region_data);
        return -1;
    }
    region_data->mapped_length = size;
    if(region_data->attach_addr != starting_addr) {
        fprintf(stderr, "Warning: shared memory region attached to %p instead of %p. %s:%d\n",
            region_data->attach_addr, starting_addr, __FILE__, __LINE__);
    }

    // close the file descrptor
    if(close(fd) == -1) {
        fprintf(stderr, "Error: close() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        munmap(region_data->attach_addr, region_data->mapped_length);
        free(region_data->file_name);
        free(region_data);
        return -1;
    }

    *return_data = region_data;
    *attach_address = region_data->attach_addr;
    return 0;
}

/*
 * the contact info of a mmap shm region has the following fields:
 * - file_name (strlen bytes terminated by '\0')
 * - file size (sizeof(size_t) bytes)
 */
void * df_shm_method_mmap_region_contact (void *method_data, df_shm_region_t region, int *length)
{
    shm_mmap_region_data_t region_data = (shm_mmap_region_data_t) region->method_data;

    void *contact_string;
    int file_name_len = strlen(region_data->file_name) + 1;
    int len = file_name_len + sizeof(size_t);
    contact_string = malloc(len);
    if(!contact_string) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return NULL;
    }
    strcpy(contact_string, region_data->file_name);
    memcpy(((char *)contact_string + file_name_len), &(region_data->file_length), sizeof(size_t));
    *length = len;
    return contact_string;
}

int df_shm_method_mmap_destroy_region (void *method_data, df_shm_region_t region)
{
    shm_mmap_method_data_t m_data = (shm_mmap_method_data_t) method_data;    
    shm_mmap_region_data_t region_data = region->method_data;

    // unmap the shm region
    if(munmap(region_data->attach_addr, region_data->mapped_length) == -1) {
        fprintf(stderr, "Error: munmap() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        return -1;    
    }

    // remove the backstore file
    if(unlink(region_data->file_name) == -1) {
        fprintf(stderr, "Error: calling unlink() on %s returns %d. %s:%d\n", region_data->file_name, 
            errno, __FILE__, __LINE__);
        return -1;    
    }
    free(region_data->file_name);    
    free(region_data);
    return 0;
}

int df_shm_method_mmap_attach_region (void *method_data, 
                                      void *contact_info, 
                                      size_t size, 
                                      void *starting_addr, 
                                      void **return_data, 
                                      void **attach_address
                                     )
{
    shm_mmap_method_data_t m_data = (shm_mmap_method_data_t) method_data;

    // create per-region data
    shm_mmap_region_data_t region_data = (shm_mmap_region_data_t) 
        malloc(sizeof(shm_mmap_region_data));
    if(!region_data) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return -1;    
    }
    
    // TODO: get shm segment info from contact info
    char *file_name = (char *) contact_info;

    // open the backstore file
    int fd = open(file_name, O_RDWR, DEFAULT_OPEN_MODE);
    if(fd == -1) {
        fprintf(stderr, "Error: calling open() on %s failed: %d %s:%d\n", 
            file_name, errno, __FILE__, __LINE__);
        free(region_data);
        return -1;
    }
    region_data->file_name = strdup(file_name);

    // TODO: pass file length through contact_info    
    region_data->file_length = *(size_t *) ((char *)contact_info + strlen(file_name) + 1);
    
    if(starting_addr != NULL && (uint64_t)starting_addr % PAGE_SIZE) {
        fprintf(stderr, "Warning: the starting address (%p) is not page-aligned. %s:%d\n", 
            starting_addr, __FILE__, __LINE__);    
    }
    
    // map the file to local address space
    region_data->attach_addr = mmap(starting_addr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(region_data->attach_addr == NULL) {
        fprintf(stderr, "Error: mmap() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        free(region_data->file_name);
        free(region_data);
        return -1;
    }    
    region_data->mapped_length = size;
    if(region_data->attach_addr != starting_addr) {
        fprintf(stderr, "Warning: shared memory region attached to %p instead of %p. %s:%d\n", 
            region_data->attach_addr, starting_addr, __FILE__, __LINE__);
    }
    
    // close the file descrptor
    if(close(fd) == -1) {
        fprintf(stderr, "Error: close() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        munmap(region_data->attach_addr, region_data->mapped_length);
        free(region_data->file_name);
        free(region_data);
        return -1;    
    }

    *return_data = region_data;
    *attach_address = region_data->attach_addr;
    return 0;
}

int df_shm_method_mmap_detach_region (void *method_data, df_shm_region_t region)
{
    shm_mmap_method_data_t m_data = (shm_mmap_method_data_t) method_data;    
    shm_mmap_region_data_t region_data = region->method_data;

    // unmap the shm region
    if(munmap(region_data->attach_addr, region_data->mapped_length) == -1) {
        fprintf(stderr, "Error: munmap() returns %d. %s:%d\n", errno, __FILE__, __LINE__);
        return -1;    
    }
    free(region_data->file_name);
    free(region_data);
    return 0;    
}

int df_shm_method_mmap_finalize (void *method_data)
{
    shm_mmap_method_data_t m_data = (shm_mmap_method_data_t) method_data;    
    free(m_data);
    return 0;
}

#endif /* HAVE_MMAP */  

