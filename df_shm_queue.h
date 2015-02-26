#ifndef _DF_SHM_QUEUE_H_
#define _DF_SHM_QUEUE_H_
/*
 * DataFabrics shared memory transport for inter-process and inter-thread
 * communication on mulitcore. 
 *
 * This header file defines a uni-directional, circular, lock-free FIFO queue
 * which can be used for either inter-thread or inter-process communication.
 *
 * written by Fang Zheng (fzheng@cc.gatech.edu)
 */

 #ifdef __cplusplus
extern "C" {
#endif

#include "df_config.h"
#include <stdint.h> 
#include <unistd.h>
#include <stddef.h>
    
/*
 * slot status: empty (ready for writing) or full (ready for reading)
 */ 
enum SLOT_FLAG {
    SLOT_FULL  = 0,
    SLOT_EMPTY = 1
};
  
/*
 * one slot in the share memory queue
 */
typedef struct _df_queue_slot {
    enum SLOT_FLAG status;        // empty or full
    size_t size;                  // size of payload in bytes
    char data[0];                 // data payload      
} df_queue_slot, *df_queue_slot_t;

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif
/*
 * the queue data structure laid out in memory
 */
typedef struct _df_queue {
    int32_t initialized;          
    uint32_t max_num_slots;       // maximum number of slots; set during initialization
    size_t max_payload_size;      // size limit of payload
    size_t slot_size;             // size limit of slot
    size_t total_size;            // total size of the queue (including this header)
    char padding[CACHE_LINE_SIZE - sizeof(int32_t) - sizeof(uint32_t) - 2*sizeof(size_t)];
     
    char slots[0];                // where slots are
} df_queue, *df_queue_t;
 
/*
 * bookkeeping data structure in sender and receiver's local memory
 */
typedef struct _df_queue_endpoint {
    int slot_index;               // index of current slot to operate on (enqueue or dequeue)
    df_queue *queue;              // point to starting address of queue in shared memory 
    df_queue_slot_t *slots;       // cached starting addresses of each every slots
    int is_sender;                // sender side (1) or receiver side (0)
} df_queue_ep, *df_queue_ep_t;

/*
 * Calculate how many bytes a queue with specified configuration would occupy.
 */
size_t df_calculate_queue_size (uint32_t max_num_slots, size_t max_payload_size);

/*
 * Create a queue at specified memory location. If the addr is NULL, then the queue will
 * be allocated at a place decided by the system. max_num_slots specifies the maximum number
 * of slots in the queue and max_payload_size specified the maximum size of payload in bytes.
 * Return a handle of df_queue (which is at addr) on success; otherwise return NULL.
 */
df_queue_t df_create_queue (void *addr, uint32_t max_num_slots, size_t max_payload_size);

/*
 * Destroy a queue. Return 0 on success and non-zero on error.
 */
int df_destroy_queue (df_queue_t queue);

/*
 * Get a sender-side endpoint handle of the queue. Return NULL on error.
 */
df_queue_ep_t df_get_queue_sender_ep (df_queue_t queue);
 
/*
 * Get a receiver-side endpoint handle of the queue. Return NULL on error.
 */ 
df_queue_ep_t df_get_queue_receiver_ep (df_queue_t queue);
 
/*
 * Destroy an endpoint handle. Return 0 on success and non-zero on error.
 */ 
int df_destroy_ep (df_queue_ep_t ep);
 
/*
 * Enqueue a vector of buffers into queue. It gets the next empy slot in queue, and copies
 * the buffers into that slot, and mark the slot as "full". This is a blocking call. Return
 * 0 on success and non-zero otherwise.
 */ 
int df_enqueue_vector (df_queue_ep_t ep, struct iovec *vec, int veccnt);

/* 
 * Enqueue a single buffer into queue. This is a blocking call. Return 0 on success and 
 * non-zero otherwise.
 */
int df_enqueue (df_queue_ep_t ep, void *data, size_t length); 
 
/*
 * Test if there is empy slot in queue for enqueue operation. Return 1 if there is; return 0
 * if there is no empty slot in queue.
 */ 
int df_is_enqueue_possible (df_queue_ep_t ep);
 
/*
 * Atomic test-and-enqueue. If there is empty slot in queue then enqueue the data, and return 0
 * on success or 1 on error. If there is no empty slot, return -1 immediately.
 * return value: 0: enqueue successful; -1: no empty slot; 1: tried enqueue but failed.
 */ 
int df_try_enqueue_vector (df_queue_ep_t ep, struct iovec *vec, int veccnt);
int df_try_enqueue (df_queue_ep_t ep, void *data, size_t length); 
 
/*
 * Dequeue datafrom the next full slot in queue. data contains a reference to the data payload.
 * *length contains the length of the payload. It is the receiver's responsibility to copy the data
 * to its own receive buffer if it wants to retain the data. This is a blocking call. Return 0 on 
 * success and non-zero on error.
 */ 
int df_dequeue (df_queue_ep_t ep, void **data, size_t *length);

/*
 * Release the current slot by receiver. When receive is done with the current slot, it calls this
 * function to mark the slot as empty. 
 */
void df_release (df_queue_ep_t ep);
 
/*
 * Test if there is full slot in queue for dequeue operation. Return 1 if there is; return 0
 * if there is no full slot in queue.
 */ 
int df_is_dequeue_possible (df_queue_ep_t ep);
 
/*
 * Atomic test-and-dequeue. If there is full slot in queue then dequeue the data, and return 0
 * on success or 1 on error. If there is no full slot, return -1 immediately.
 * return value: 0: dequeue successful; -1: no full slot; 1: tried dequeue but failed.
 */ 
int df_try_dequeue (df_queue_ep_t ep, void **data, size_t *length); 

 
#ifdef __cplusplus
}
#endif
  
#endif
