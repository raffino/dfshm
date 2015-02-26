#ifndef _DF_SHM_METHOD_HOOKS_H_
#define _DF_SHM_METHOD_HOOKS_H_

#include "df_shm.h"
#include "df_config.h"

/*
 * Declare underlying shared memory method callback functions here.
 */

#ifdef HAVE_MMAP
/*
 * DF_SHM_METHOD_MMAP: shared memory backed by a mmap()-ed file
 */ 
int df_shm_method_mmap_init (void *input_data, void **method_data);
int df_shm_method_mmap_create_region (void *method_data, size_t size, void *starting_addr, void **return_data, void **attach_addr); 
int df_shm_method_mmap_create_named_region (void *method_data, void *name, int name_size, size_t size, void *starting_addr, void **return_data, void **attach_addr); 
void * df_shm_method_mmap_region_contact (void *method_data, df_shm_region_t region, int *length); 
int df_shm_method_mmap_destroy_region (void *method_data, df_shm_region_t region); 
int df_shm_method_mmap_attach_region (void *method_data, void *contact_info, size_t size, void *starting_addr, void **return_data, void **attach_addr); 
int df_shm_method_mmap_detach_region (void *method_data, df_shm_region_t region); 
int df_shm_method_mmap_finalize (void *method_data);
#endif

#ifdef HAVE_SYSV
/*
 * DF_SHM_METHOD_SYSV: System V shared memory
 */
int df_shm_method_sysv_init (void *input_data, void **method_data);
int df_shm_method_sysv_create_region (void *method_data, size_t size, void *starting_addr, void **return_data, void **attach_addr); 
int df_shm_method_sysv_create_named_region (void *method_data, void *name, int name_size, size_t size, void *starting_addr, void **return_data, void **attach_addr); 
void * df_shm_method_sysv_region_contact (void *method_data, df_shm_region_t region, int *length); 
int df_shm_method_sysv_destroy_region (void *method_data, df_shm_region_t region); 
int df_shm_method_sysv_attach_region (void *method_data, void *contact_info, size_t size, void *starting_addr, void **return_data, void **attach_addr); 
int df_shm_method_sysv_detach_region (void *method_data, df_shm_region_t region); 
int df_shm_method_sysv_finalize (void *method_data);
#endif 

#ifdef HAVE_POSIX_SHM
/*
 * DF_SHM_METHOD_POSIX_SHM: POSIX shared memory
 */
int df_shm_method_posixshm_init (void *input_data, void **method_data);
int df_shm_method_posixshm_create_region (void *method_data, size_t size, void *starting_addr, void **return_data, void **attach_addr); 
int df_shm_method_posixshm_create_named_region (void *method_data, void *name, int name_size, size_t size, void *starting_addr, void **return_data, void **attach_addr); 
void * df_shm_method_posixshm_region_contact (void *method_data, df_shm_region_t region, int *length); 
int df_shm_method_posixshm_destroy_region (void *method_data, df_shm_region_t region); 
int df_shm_method_posixshm_attach_region (void *method_data, void *contact_info, size_t size, void *starting_addr, void **return_data, void **attach_addr); 
int df_shm_method_posixshm_detach_region (void *method_data, df_shm_region_t region); 
int df_shm_method_posixshm_finalize (void *method_data);
#endif

/*
 * Load callback functions for the specified underlying shm method.
 */
void load_method_callbacks(enum DF_SHM_METHOD method, df_shm_method_t m)
{
    if(method == DF_SHM_METHOD_MMAP) {
#ifdef HAVE_MMAP
        m->init_func  = df_shm_method_mmap_init;
        m->create_region_func = df_shm_method_mmap_create_region;
        m->create_named_region_func = df_shm_method_mmap_create_named_region;
        m->region_contact_func = df_shm_method_mmap_region_contact;
        m->destroy_region_func = df_shm_method_mmap_destroy_region;
        m->attach_region_func = df_shm_method_mmap_attach_region;
        m->detach_region_func = df_shm_method_mmap_detach_region;
        m->finalize_func = df_shm_method_mmap_finalize;
#else
        fprintf(stderr, "Error: df_shm/mmap method is not available\n");
#endif
        return;
    }

    if(method == DF_SHM_METHOD_SYSV) {
#ifdef HAVE_SYSV
        m->init_func  = df_shm_method_sysv_init;
        m->create_region_func = df_shm_method_sysv_create_region;
        m->create_named_region_func = df_shm_method_sysv_create_named_region;
        m->region_contact_func = df_shm_method_sysv_region_contact;
        m->destroy_region_func = df_shm_method_sysv_destroy_region;
        m->attach_region_func = df_shm_method_sysv_attach_region;
        m->detach_region_func = df_shm_method_sysv_detach_region;
        m->finalize_func = df_shm_method_sysv_finalize;
#else
        fprintf(stderr, "Error: df_shm/sysv method is not available\n");
#endif
        return;
    }

    if(method == DF_SHM_METHOD_POSIX_SHM) {
#ifdef HAVE_POSIX_SHM
        m->init_func  = df_shm_method_posixshm_init;
        m->create_region_func = df_shm_method_posixshm_create_region;
        m->create_named_region_func = df_shm_method_posixshm_create_named_region;
        m->region_contact_func = df_shm_method_posixshm_region_contact;
        m->destroy_region_func = df_shm_method_posixshm_destroy_region;
        m->attach_region_func = df_shm_method_posixshm_attach_region;
        m->detach_region_func = df_shm_method_posixshm_detach_region;
        m->finalize_func = df_shm_method_posixshm_finalize;
#else
        fprintf(stderr, "Error: df_shm/posix_shm method is not available\n");
#endif
        return;
    }
}
 
#endif
