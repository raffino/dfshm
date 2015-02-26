#ifndef _DF_SHM_H_
#define _DF_SHM_H_
/*
 * DataFabrics shared memory transport for inter-process and inter-thread
 * communication on mulitcore. 
 *
 * This header file provides an abstract interface to manipulate shared 
 * memory (create, attach, detach, destroy) on top of several underlying
 * shared memory mechanisms (SysV, mmap, POSIX shm). 
 *
 * written by Fang Zheng (fzheng@cc.gatech.edu)
 */

#ifdef __cplusplus
extern "C" {
#endif
 
#include <unistd.h>
#include <stddef.h>
    
/* Macros */
#define DF_SHM_UNKNOWN_PID ((pid_t) -1)

/*
 * supported underlying shared memory method
 */ 
enum DF_SHM_METHOD {
    DF_SHM_METHOD_MMAP = 0,      // shared memory backed up a mmap()-ed file
    DF_SHM_METHOD_SYSV = 1,      // System V shared memory
    DF_SHM_METHOD_POSIX_SHM = 2, // POSIX shared memory
    DF_SHM_NUM_METHODS 
}; 

//typedef struct _df_shm_region df_shm_region, *df_shm_region_t;
/*
 * a shared memory region
 */
typedef struct _df_shm_region {
    size_t size;
    void *starting_addr; // attaching address
    pid_t creator_id;    // the pid of process who created this region
    void *method_data;
    struct _df_shm_method *shm_method;
    struct _df_shm_region *next;
} df_shm_region, *df_shm_region_t;

typedef int (* shm_method_init_func) (void *input_data, void **method_data);

typedef int (* shm_method_create_region_func) (void *method_data, size_t size, void *starting_addr, void **return_data, void **attach_addr); 

typedef int (* shm_method_create_named_region_func) (void *method_data, void *name, int name_size, size_t size, void *starting_addr, void **return_data, void **attach_addr); 

typedef void * (* shm_method_region_contact_info_func) (void *method_data, df_shm_region_t region, int *length); 
 
typedef int (* shm_method_destroy_region_func) (void *method_data, df_shm_region_t region); 
 
typedef int (* shm_method_attach_region_func) (void *method_data, void *contact_info, size_t size, void *starting_addr, void **return_data, void **attach_addr); 
 
typedef int (* shm_method_detach_region_func) (void *method_data, df_shm_region_t region);
 
typedef int (* shm_method_finalize_func) (void *method_data);

/*
 * underlying shared memory method handle
 */ 
typedef struct _df_shm_method {
    enum DF_SHM_METHOD method;     
    int initialized;
    void *method_data;   // data private to this method
    df_shm_region_t created_regions;  // NULL-terminated list of shm regions created by this process
    int num_created_regions;
    df_shm_region_t foreign_regions;  // NULL-terminated list of shm regions attached by this process
    int num_foreign_regions;
    shm_method_init_func init_func;
    shm_method_create_region_func create_region_func;
    shm_method_create_named_region_func create_named_region_func;
    shm_method_region_contact_info_func region_contact_func;
    shm_method_destroy_region_func destroy_region_func;
    shm_method_attach_region_func attach_region_func;
    shm_method_detach_region_func detach_region_func;
    shm_method_finalize_func finalize_func;
} df_shm_method, *df_shm_method_t;

/*
 * Initialize specific underlying shared memory method and return a method
 * handle. This handle should be used in subsequent calls. If the return
 * value is NULL, then the call is failed.
 */
df_shm_method_t df_shm_init (enum DF_SHM_METHOD method, 
                             void *method_init_data
                            );

/*
 * Create a shared memory region which is 'size' bytes and attach it to calling 
 * process' address space at the address specified by starting_addr. Return a 
 * handle to the shared memory region if successful; otherwise return NULL.
 */
df_shm_region_t df_create_shm_region (df_shm_method_t method, 
                                      size_t size, 
                                      void *starting_addr
                                     ); 

/*
 * Create a shared memory region at a location specified by name parameter.
 * the underlying shm method will interpret the opaque 'name' object and create
 * the shared memory region. This is useful to create a shared memory region at
 * a "well-known" place so other processes can directly attach to it without need
 * to exchange the region's contact info.
 */
df_shm_region_t df_create_named_shm_region (df_shm_method_t method, 
                                            void *name, 
                                            int name_size, 
                                            size_t size, 
                                            void *starting_addr
                                           );

/*
 * Get the contact info of a shm region. The contact info is opaque at df level
 * and will be interpreted by underlying shm method to locate a shm region.
 * Return value points to data which can be used by other processes to
 * locate this shm region. The length parameter returns the size of the return value.
 * Return NULL on error.
 */
void * df_shm_region_contact_info (df_shm_method_t method, 
                                   df_shm_region_t region, 
                                   int *length
                                  );

/*
 * Destroy a shared memory region. If the creator process calls this function, 
 * it will detach the shared memory region, and recycle any resources associated
 * with the region (depending on the underlying shm method), and free the region 
 * data structure. If the calling process attached this region created by some 
 * other process, it will detach the region and free the region data structure.
 * Return 0 means success. Non-zero return value means error.
 */ 
int df_destroy_shm_region (df_shm_region_t region); 

/*
 * Attach to a shared memory region which is created by some other process and can be 
 * located by contact_info. The underlying shm method will intepret contact_info.
 * starting_addr specified the local address to which the shm region should be attached.
 * Return a handle of shm_region if successful; otherwise return NULL.
 */ 
df_shm_region_t df_attach_shm_region (df_shm_method_t method,  
                                      pid_t creator_id,
                                      void *contact_info, 
                                      size_t size, 
                                      void *starting_addr
                                     ); 

/*
 * Attach to a named shared memory region which is usually created by some other 
 * process with df_create_named_shm_region() function. 
 */
df_shm_region_t df_attach_named_shm_region (df_shm_method_t method,
                                            void *name,
                                            int name_size,
                                            size_t size,
                                            void *starting_addr
                                           );

/*
 * Detach a shared memory region from local address space. Return 0 on success and non-zero or error.
 * Either the creator process or process which attached this region can detach the region.
 */ 
int df_detach_shm_region (df_shm_region_t region);

/*
 * Finalize function when finishing using the shared memory method. This function
 * performs various cleanups and free the method handle data structure. Return 0 
 * on success and non-zero otherwise.
 */ 
int df_shm_finalize (df_shm_method_t method);

/*
 * convert a local virtual address to offset relative to the starting address of shm region
 */
#define ADDR2OFFSET(region, addr)  (size_t)((char *)(addr) - (char *)(region->starting_addr))

/*
 * convert a offset relative to the starting address of a shm region to a local virtual address
 */
#define OFFSET2ADDR(region, offset) (void *)((char *)(region->starting_addr) + (size_t)(offset)) 

#ifdef __cplusplus
}
#endif
  
#endif
