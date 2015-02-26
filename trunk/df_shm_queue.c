/*
 * DataFabrics shared memory transport for inter-process and inter-thread
 * communication on mulitcore. 
 *
 * This file implements a uni-directional, circular, lock-free FIFO queue
 * which can be used for either inter-thread or inter-process communication.
 *
 * written by Fang Zheng (fzheng@cc.gatech.edu)
 */

#include "df_config.h"
#include <stdint.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <unistd.h>
#include <sys/uio.h>
#include <assert.h>
#include "df_shm_queue.h"
    
/*
 * Calculate how many bytes a queue slot with specified configuration would occupy.
 */
size_t df_calculate_slot_size (size_t max_payload_size)
{
    assert(max_payload_size > 0);

    size_t slot_size = sizeof(df_queue_slot) + max_payload_size;
    if(slot_size % CACHE_LINE_SIZE) {
        slot_size += CACHE_LINE_SIZE - (slot_size % CACHE_LINE_SIZE);
    }
    return slot_size;
}

/*
 * Calculate how many bytes a queue with specified configuration would occupy.
 */
size_t df_calculate_queue_size (uint32_t max_num_slots, size_t max_playoad_size)
{
    assert(max_num_slots > 0);
    
    size_t per_slot_size = df_calculate_slot_size(max_playoad_size);
    size_t total_size = sizeof(df_queue) + max_num_slots * per_slot_size;
    return total_size;
}

/*
 * Create a queue at specified memory location. max_num_slots specifies the maximum number
 * of slots in the queue and max_payload_size specified the maximum size of payload in bytes.
 * Return a handle of df_queue (which is at addr) on success; otherwise return NULL.
 */
df_queue_t df_create_queue (void *addr, uint32_t max_num_slots, size_t max_payload_size)
{
    assert(max_num_slots > 0);
    assert(max_payload_size > 0);
    assert(addr != NULL);
    
    // the total size of the queue
    size_t total_size = df_calculate_queue_size(max_num_slots, max_payload_size);

    df_queue_t queue = (df_queue_t) addr;
    queue->initialized = 0;
    queue->max_num_slots = max_num_slots;
    queue->max_payload_size = max_payload_size;
    queue->slot_size = df_calculate_slot_size(max_payload_size);
    queue->total_size = df_calculate_queue_size(max_num_slots, max_payload_size);

    // initialize slots
    df_queue_slot_t slot;
    char *slots_start = queue->slots;
    int i;
    for(i = 0; i < max_num_slots; i ++) {
        slot = (df_queue_slot_t) (slots_start + i * queue->slot_size);
          slot->status = SLOT_EMPTY; 
        slot->size = 0;
    }

    queue->initialized = 1;    
    return queue;    
}

/*
 * Destroy a queue. Return 0 on success and non-zero on error.
 */
int df_destroy_queue (df_queue_t queue)
{
    if(queue) {
        queue->initialized = 0;
        return 0;
    }
    else {
        fprintf(stderr, "Error: queue is NULL. %s:%d\n", __FILE__, __LINE__);
        return -1;
    }    
}

/*
 * Create an endpoint handle of a queue
 */
df_queue_ep_t df_internal_get_queue_ep (df_queue_t queue, int is_sender)
{
    assert(queue != NULL);
    
    if(!queue->initialized) {
        fprintf(stderr, "Error: queue is not initialized. %s:%d\n", __FILE__, __LINE__);
        return NULL;
    }
    df_queue_ep_t ep = (df_queue_ep_t) malloc(sizeof(df_queue_ep));
    if(!ep) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        return NULL;
    }    
    ep->slots = (df_queue_slot_t *) malloc(queue->max_num_slots * sizeof(df_queue_slot_t));
    if(!ep->slots) {
        fprintf(stderr, "Error: cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        free(ep);
        return NULL;        
    }
    
    // record the starting address of each slot for later use
    int i;
    char *slot_start = queue->slots;
    for(i = 0; i < queue->max_num_slots; i ++) {
        ep->slots[i] = (df_queue_slot_t)(slot_start + i * queue->slot_size);
    }
    ep->slot_index = 0;
    ep->queue = queue;
    ep->is_sender= is_sender;    
    return ep;
}

/*
 * Get a sender-side endpoint handle of the queue. Return NULL on error.
 */
df_queue_ep_t df_get_queue_sender_ep (df_queue_t queue)
{
    assert(queue != NULL);

    df_queue_ep_t ep = df_internal_get_queue_ep(queue, 1);
    return ep;
}
 
/*
 * Get a receiver-side endpoint handle of the queue. Return NULL on error.
 */ 
df_queue_ep_t df_get_queue_receiver_ep (df_queue_t queue)
{
    assert(queue != NULL);

    df_queue_ep_t ep = df_internal_get_queue_ep(queue, 0);
    return ep;
}
 
/*
 * Destroy an endpoint handle. Return 0 on success and non-zero on error.
 */ 
int df_destroy_ep (df_queue_ep_t ep)
{
    assert(ep != NULL);
    
    free(ep->slots);    
    free(ep);
    return 0;
}
 
/*
 * Enqueue a vector of buffers into queue. It gets the next empy slot in queue, and copies
 * the buffers into that slot, and mark the slot as "full". This is a blocking call. Return
 * 0 on success and non-zero otherwise.
 */ 
int df_enqueue_vector (df_queue_ep_t ep, struct iovec *vec, int veccnt)
{
    assert(ep != NULL);
    assert(ep->queue != NULL);
    assert(ep->queue->initialized);
    assert(ep->is_sender);

    int i;    
    size_t size = 0;
    for(i = 0; i < veccnt; i ++) {
        size += vec[i].iov_len;
    }
    if(size <= ep->queue->max_payload_size) {
        df_queue_slot_t current_slot = ep->slots[ep->slot_index];
        
        // make sure the slot is empty
        while(current_slot->status != SLOT_EMPTY) { }

        // copy data into the slot
        char *dest = current_slot->data;
        for(i = 0; i < veccnt; i ++) {
            memcpy(dest, vec[i].iov_base, vec[i].iov_len);
            dest += vec[i].iov_len;
        }
        current_slot->size = size;

        // TODO: memory fence
        //__sync_synchronize();

        // mark the slot as full
        current_slot->status = SLOT_FULL;
        
        // advance to next slot
        ep->slot_index = (ep->slot_index + 1) % ep->queue->max_num_slots;
        return 0;        
    }
    else { // TODO: handle large message seperately
        fprintf(stderr, "Error: payload size (%lu) exceeds queue limit (%lu). %s:%d\n", 
            size, ep->queue->max_payload_size, __FILE__, __LINE__);
        return -1;
    }
}

/* 
 * Enqueue a single buffer into queue. This is a blocking call. Return 0 on success and 
 * non-zero otherwise.
 */
int df_enqueue (df_queue_ep_t ep, void *data, size_t length)
{
    struct iovec vec;
    vec.iov_base = data;
    vec.iov_len = length;
    return df_enqueue_vector(ep, &vec, 1);
}
 
/*
 * Test if there is empy slot in queue for enqueue operation. Return 1 if there is; return 0
 * if there is no empty slot in queue.
 */ 
int df_is_enqueue_possible (df_queue_ep_t ep)
{
    assert(ep != NULL);
    assert(ep->queue != NULL);
    assert(ep->queue->initialized);
    
    return (ep->slots[ep->slot_index]->status == SLOT_EMPTY)? 1: 0;
}
 
/*
 * Atomic test-and-enqueue. If there is empty slot in queue then enqueue the data, and return 0
 * on success or 1 on error. If there is no empty slot, return -1 immediately.
 * return value: 0: enqueue successful; -1: no empty slot; 1: tried enqueue but failed.
 */ 
int df_try_enqueue_vector (df_queue_ep_t ep, struct iovec *vec, int veccnt)
{
    assert(ep != NULL);
    assert(ep->queue != NULL);
    assert(ep->queue->initialized);
    assert(ep->is_sender);

    int i;    
    size_t size = 0;
    for(i = 0; i < veccnt; i ++) {
        size += vec[i].iov_len;
    }
    if(size <= ep->queue->max_payload_size) {        
        df_queue_slot_t current_slot = ep->slots[ep->slot_index];
        
        // test if the slot is empty
        if(current_slot->status != SLOT_EMPTY) { 
            return -1;
        }

        // copy data into the slot
        char *dest = current_slot->data;
        for(i = 0; i < veccnt; i ++) {
            memcpy(dest, vec[i].iov_base, vec[i].iov_len);
            dest += vec[i].iov_len;
        }
        current_slot->size = size;

        // TODO: memory fence
        //__sync_synchronize();

        // mark the slot as full
        current_slot->status = SLOT_FULL;
        
        // advance to next slot
        ep->slot_index = (ep->slot_index + 1) % ep->queue->max_num_slots;
        return 0;        
    }
    else { // TODO: handle large message seperately
        fprintf(stderr, "Error: payload size (%lu) exceeds queue limit (%lu). %s:%d\n", 
            size, ep->queue->max_payload_size, __FILE__, __LINE__);
        return 1;
    }    
}

int df_try_enqueue (df_queue_ep_t ep, void *data, size_t length)
{
    struct iovec vec;
    vec.iov_base = data;
    vec.iov_len = length;
    return df_try_enqueue_vector(ep, &vec, 1);
}
 
/*
 * Dequeue data from the next full slot in queue. *data points to the data payload.
 * *length contains the length of the payload. It is the receiver's responsibility to copy the data
 * to its own receive buffer if it wants to retain the data. This is a blocking call. Return 0 on 
 * success and non-zero on error.
 */ 
int df_dequeue (df_queue_ep_t ep, void **data, size_t *length)
{
    assert(ep != NULL);
    assert(ep->queue != NULL);
    assert(ep->queue->initialized);
    assert(ep->is_sender == 0);
    assert(data != NULL);
    assert(length != NULL);

    df_queue_slot_t current_slot = ep->slots[ep->slot_index];
        
    // make sure the slot is full
    while(current_slot->status != SLOT_FULL) { }

    // TODO: memory fence
    //__sync_synchronize();

    *data = (void *) current_slot->data;
    *length = current_slot->size;      
    return 0;    
}

/*
 * Release the current slot by receiver. When receive is done with the current slot, it calls this
 * function to mark the slot as empty. 
 */
void df_release (df_queue_ep_t ep)
{
    assert(ep != NULL);
    assert(ep->queue != NULL);
    assert(ep->queue->initialized);
    assert(ep->is_sender == 0);

    df_queue_slot_t current_slot = ep->slots[ep->slot_index];
    current_slot->size = 0;
    
    // TODO: memory fence
    //__sync_synchronize();

    // mark the slot as empty
    current_slot->status = SLOT_EMPTY;    
    
    // advance to wait for new data on the next slot
    ep->slot_index = (ep->slot_index + 1) % ep->queue->max_num_slots;
}
 
/*
 * Test if there is full slot in queue for dequeue operation. Return 1 if there is; return 0
 * if there is no full slot in queue.
 */ 
int df_is_dequeue_possible (df_queue_ep_t ep)
{
    assert(ep != NULL);
    assert(ep->queue != NULL);
    assert(ep->queue->initialized);
    assert(ep->is_sender == 0);
    
    return (ep->slots[ep->slot_index]->status == SLOT_FULL)? 1: 0;
}
 
/*
 * Atomic test-and-dequeue. If there is full slot in queue then dequeue the data, and return 0
 * on success or 1 on error. If there is no full slot, return -1 immediately.
 * return value: 0: dequeue successful; -1: no full slot; 1: tried dequeue but failed.
 */ 
int df_try_dequeue (df_queue_ep_t ep, void **data, size_t *length)
{
    assert(ep != NULL);
    assert(ep->queue != NULL);
    assert(ep->queue->initialized);
    assert(ep->is_sender == 0);
    assert(data != NULL);
    assert(length != NULL);

    df_queue_slot_t current_slot = ep->slots[ep->slot_index];
        
    // make sure the slot is full
    if(current_slot->status != SLOT_FULL) { 
        return -1;
    }

    // TODO: memory fence
    //__sync_synchronize();

    *data = (void *) current_slot->data;
    *length = current_slot->size;  
    return 0;    
}
