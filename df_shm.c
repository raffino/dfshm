/*
 * DataFabrics shared memory transport for inter-process and inter-thread
 * communication on mulitcore. 
 *
 * This file implements an abstract interface to manipulate shared 
 * memory (create, attach, detach, destroy) on top of several underlying
 * shared memory mechanisms (SysV, mmap, POSIX shm). 
 *
 * written by Fang Zheng (fzheng@cc.gatech.edu)
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>    
#include <sys/types.h>
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "df_shm.h"
#include "df_shm_method_hooks.h"

/*
 * Initialize specific underlying shared memory method and return a method
 * handle. This handle should be used in subsequent calls. If the return
 * value is NULL, then the call is failed.
 */
df_shm_method_t df_shm_init (enum DF_SHM_METHOD method, void *method_init_data)
{
    df_shm_method_t m = (df_shm_method_t) malloc(sizeof(df_shm_method));
    if(!m) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return NULL;
    }

    // load underlying method callback functions
    if((int)method >= 0 && method < DF_SHM_NUM_METHODS) {
        load_method_callbacks(method, m);            
    }
    else {
        fprintf(stderr, "Error: method (%d) is not valid. %s:%d\n", method, __FILE__, __LINE__);    
        free(m);
        return NULL;
    }

    // initialize the method-private data 
    if(m->init_func) {
        int rc = (*m->init_func)(method_init_data, &(m->method_data));
        if(rc != 0) {
            fprintf(stderr, "Error: calling method's init function: return %d %s:%d\n", 
                rc, __FILE__, __LINE__);
            free(m);
            return NULL;
        }
    }
    
    m->created_regions = NULL;
    m->num_created_regions = 0;
    m->foreign_regions = NULL;
    m->num_foreign_regions = 0;    
    m->initialized = 1;
    return m;    
}

/*
 * Add a region to a list.
 */
void add_region_to_list(df_shm_region_t *list, df_shm_region_t r)
{
    // insert at the beginning of the list
    r->next = *list;
    *list = r;
}

/*
 * Remove a region from a list. Return 0 on success and non-zero on error.
 */
int remove_region_from_list(df_shm_region_t *list, df_shm_region_t r)
{
    if(!*list) {
        fprintf(stderr, "Warning: try to remove region from an empty list. %s:%d\n", __FILE__, __LINE__);
        return -1;
    }
    df_shm_region_t temp = *list, last = NULL;
    while(temp) {
        if(temp == r) {
            if(last == NULL) {
                *list = temp->next;
            }
            else {
                last->next = temp->next;
            }
            return 0;
        }
        last = temp;
        temp = temp->next;
    }
    fprintf(stderr, "Warning: the region is not found in list. %s:%d\n", __FILE__, __LINE__);
    return 1;
}

/*
 * Create a shared memory region which is 'size' bytes and attach it to calling 
 * process' address space at the address specified by starting_addr. Return a 
 * handle to the shared memory region if successful; otherwise return NULL.
 */
df_shm_region_t df_create_shm_region (df_shm_method_t method, 
                                      size_t size, 
                                      void *starting_addr
                                     )
{
    assert(method != NULL);
    assert(method->initialized == 1);
    assert(size > 0);    
    
    df_shm_region_t region = (df_shm_region_t) malloc(sizeof(df_shm_region));
    if(!region) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return NULL;
    }    
    if(method->create_region_func) {
        int rc = (*method->create_region_func) (method->method_data, size, starting_addr,
            (void **)&(region->method_data), (void **)&(region->starting_addr));
        if(rc) {
            fprintf(stderr, "Error: method's create_region callback returns error: %d. %s:%d\n", rc, __FILE__, __LINE__);
            free(region);
            return NULL;
        }
    }
    else {
        fprintf(stderr, "Warning: method's create_region callback is not registered. %s:%d\n", 
            __FILE__, __LINE__);    
    }
    region->size = size;
    region->creator_id = getpid(); // the pid of the creator process
    region->shm_method = method;
    add_region_to_list(&method->created_regions, region);
    method->num_created_regions ++;
    return region;
}

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
                                           )
{
    assert(method != NULL);
    assert(method->initialized == 1);
    assert(size > 0);

    df_shm_region_t region = (df_shm_region_t) malloc(sizeof(df_shm_region));
    if(!region) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return NULL;
    }
    if(method->create_region_func) {
        int rc = (*method->create_named_region_func) (method->method_data, name, name_size, 
            size, starting_addr,(void **)&(region->method_data), (void **)&(region->starting_addr));
        if(rc) {
            fprintf(stderr, "Error: method's create_region callback returns error: %d. %s:%d\n", rc, __FILE__, __LINE__);
            free(region);
            return NULL;
        }
    }
    else {
        fprintf(stderr, "Warning: method's create_region callback is not registered. %s:%d\n",
            __FILE__, __LINE__);
    }
    region->size = size;
    region->creator_id = getpid(); // the pid of the creator process
    region->shm_method = method;
    add_region_to_list(&method->created_regions, region);
    method->num_created_regions ++;
    return region;
}

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
                                  )
{
    assert(method != NULL);
    assert(method->initialized == 1);
    assert(region != NULL);    
    assert(length != NULL);

    void *contact_info = NULL;
    if(method->region_contact_func) {
        contact_info = (*method->region_contact_func) (method->method_data, region, length);
    }
    else {
        fprintf(stderr, "Warning: method's region_contact_info callback is not registered. %s:%d\n",
            __FILE__, __LINE__);    
    }
    return contact_info;
}

/*
 * Attach a shared memory region which is created by another process (of pid creator_id) 
 * and can be located by contact_info. The underlying shm method will intepret contact_info.
 * starting_addr specified the local address to which the shm region should be attached.
 * Return a handle of shm_region if successful; otherwise return NULL.
 *
 * Note: when creating a shm region using df_create_shm_region(), the creator process 
 * attaches the region to its local address space internally so it should not call 
 * df_attach_shm_region().
 */ 
df_shm_region_t df_attach_shm_region (df_shm_method_t method, 
                                      pid_t creator_id,
                                      void *contact_info,
                                      size_t size,                                      
                                      void *starting_addr)
{
    assert(method != NULL);
    assert(method->initialized == 1);
    assert(contact_info != NULL);    

    df_shm_region_t region = (df_shm_region_t) malloc(sizeof(df_shm_region));
    if(!region) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return NULL;
    }    
    if(method->attach_region_func) {
        int rc = (*method->attach_region_func) (method->method_data, contact_info,  
            size, starting_addr, (void *)&(region->method_data), (void **)&(region->starting_addr));
        if(rc) {
            fprintf(stderr, "Error: method's attach_region callback returns error: %d. %s:%d\n", 
                rc, __FILE__, __LINE__);
            free(region);
            return NULL;
        }
    }
    else {
        fprintf(stderr, "Warning: method's attach_region callback is not registered. %s:%d\n",
            __FILE__, __LINE__);    
    }
    region->size = size;
    region->creator_id = creator_id;
    region->shm_method = method;
    add_region_to_list(&method->foreign_regions, region);
    method->num_foreign_regions ++;
    return region;    
}

/*
 * Attach to a named shared memory region which is usually created by some other
 * process with df_create_named_shm_region() function.
 */
df_shm_region_t df_attach_named_shm_region (df_shm_method_t method,
                                            void *name, 
                                            int name_size,
                                            size_t size,
                                            void *starting_addr
                                           )
{
    assert(method != NULL);
    assert(method->initialized == 1);
    assert(name != NULL);

    df_shm_region_t region = (df_shm_region_t) malloc(sizeof(df_shm_region));
    if(!region) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return NULL;
    }
    if(method->attach_region_func) {
        int rc = (*method->attach_region_func) (method->method_data, name,
            size, starting_addr, (void *)&(region->method_data), (void **)&(region->starting_addr));
        if(rc) {
            fprintf(stderr, "Error: method's attach_region callback returns error: %d. %s:%d\n",
                rc, __FILE__, __LINE__);
            free(region);
            return NULL;
        }
    }
    else {
        fprintf(stderr, "Warning: method's attach_region callback is not registered. %s:%d\n",
            __FILE__, __LINE__);
    }
    region->size = size;
    region->creator_id = DF_SHM_UNKNOWN_PID;
    region->shm_method = method;
    add_region_to_list(&method->foreign_regions, region);
    method->num_foreign_regions ++;
    return region;
}

/*
 * Detach a shared memory region from local address space. Return 0 on success and non-zero or error.
 * Either the creator process or process which attached this region can detach the region.
 */ 
int df_detach_shm_region (df_shm_region_t region)
{
    assert(region != NULL);    
    assert(region->shm_method != NULL);
    assert(region->shm_method->initialized == 1);
    
    df_shm_method_t method = region->shm_method;
    
    if(method->detach_region_func) {
        int rc = (*method->detach_region_func) (method->method_data, region);
        if(rc) {
            fprintf(stderr, "Error: method's detach_region callback returns error: %d. %s:%d\n", 
                rc, __FILE__, __LINE__);
            free(region);
            return -1;
        }
    }
    else {
        fprintf(stderr, "Warning: method's detach_region callback is not registered. %s:%d\n", 
            __FILE__, __LINE__);    
    }
    remove_region_from_list(&method->foreign_regions, region);    
    method->num_foreign_regions --;    
    free(region); // TODO:?
    return 0;
}

/*
 * Destroy a shared memory region. If the creator process calls this function, 
 * it will detach the shared memory region, and recycle any resources associated
 * with the region (depending on the underlying shm method), and free the region 
 * data structure. If the calling process attached this region created by some 
 * other process, it will detach the region and free the region data structure.
 * Return 0 means success. Non-zero return value means error.
 */ 
int df_destroy_shm_region (df_shm_region_t region)
{
    assert(region != NULL);    
    assert(region->shm_method != NULL);
    assert(region->shm_method->initialized == 1);
    
    df_shm_method_t method = region->shm_method;
    
    if(region->creator_id == getpid()) { // the region is created by this process
        if(method->destroy_region_func) {
            int rc = (*method->destroy_region_func) (method->method_data, region);
            if(rc) {
                fprintf(stderr, "Error: method's destroy_region callback returns error: %d. %s:%d\n", 
                    rc, __FILE__, __LINE__);
                free(region);
                return -1;
            }
        }
        else {
            fprintf(stderr, "Warning: method's destroy_region callback is not registered. %s:%d\n", 
                __FILE__, __LINE__);    
        }        
        remove_region_from_list(&method->created_regions, region);
        free(region);
        return 0;
    }
    else { // i'm not the creator but attached this region
        return df_detach_shm_region(region);
    }
}

/*
 * Finalize function when finishing using the shared memory method. This function
 * performs various cleanups and free the method handle data structure. Return 0 
 * on success and non-zero otherwise.
 */ 
int df_shm_finalize (df_shm_method_t method)
{
    assert(method != NULL);
    assert(method->initialized == 1);

    // make sure all regions are detached and destroyed
    df_shm_region_t r, next;
    r = method->created_regions;
    while(r) {
        next = r->next;
        df_destroy_shm_region(r);
        r = next;
    }
    r = method->foreign_regions;
    while(r) {
        next = r->next;
        df_detach_shm_region(r);
        r = next;
    }
    
    if(method->finalize_func) {
        int rc = (*method->finalize_func) (method->method_data);
        if(rc) {
            fprintf(stderr, "Error: method's finalize callback returns error: %d. %s:%d\n", 
                rc, __FILE__, __LINE__);
            return -1;
        }    
        free(method);
        return 0;
    }
    else {
        fprintf(stderr, "Warning: method's finalize callback is not registered. %s:%d\n", 
            __FILE__, __LINE__);    
        return 0;
    }
}
