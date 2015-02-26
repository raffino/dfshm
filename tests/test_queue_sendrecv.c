/*
 * This test program excercises DF's shm queue routines.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <mpi.h>
#include "df_shm.h"
#include "df_shm_queue.h"
#include "df_config.h"

// test parameters
enum DF_SHM_METHOD shm_method = DF_SHM_METHOD_MMAP;
size_t max_payload_size = 2048;
size_t num_slots = 5;
uint64_t num_msgs = 1000000;
size_t msg_size = 16;

void sender();
void receiver();

int main (int argc, char *argv[])
{
    int rank, size;

    MPI_Init (&argc, &argv);      
    MPI_Comm_rank (MPI_COMM_WORLD, &rank);        
    MPI_Comm_size (MPI_COMM_WORLD, &size);        
    if(size != 2) {
        fprintf(stderr, "The test requires 2 MPI processes.\n");    
        MPI_Finalize();
        return -1;
    }
    printf( "Hello world from process %d of %d\n", rank, size );

    if(rank==0) {
        sender();
    }
    else {
        receiver();
    }
    MPI_Finalize();
    return 0;
}

void sender()
{
    // choose the underlying shm method
    df_shm_method_t df_shm_handle = df_shm_init(shm_method, NULL);
    if(!df_shm_handle) {
        fprintf(stderr, "Cannot initialize shm method %d. %s:%d\n", 
            shm_method, __FILE__, __LINE__);
        exit(-1);
    }
    
    // create a shm region and two FIFO queues in the region
    // size of each shm region is calculated according to the queue size
    size_t queue_size = df_calculate_queue_size(num_slots, max_payload_size);
    size_t region_size = 2 * queue_size + 3 * sizeof(uint64_t); // meta-data at beginning
    if(region_size % PAGE_SIZE) {
        region_size += PAGE_SIZE - (region_size % PAGE_SIZE);
    }
    df_shm_region_t shm_region = df_create_shm_region(df_shm_handle, region_size, NULL);
    if(!shm_region) {
        fprintf(stderr, "Cannot create region. %s:%d\n", __FILE__, __LINE__);
        exit(-1);
    }

    // the shm region is laid out in memory as follows:
    // starting addr:
    // creator pid (8 byte)
    // starting offset to the starting address of send queue (8 byte)
    // starting offset to the starting address of recv queue (8 byte)
    // send queue (starting address cacheline aligned)
    // recv queue (starting address cacheline aligned)
    char *ptr = (char *) shm_region->starting_addr; // start of region, should be page-aligned
    *((uint64_t *) ptr) = (uint64_t) shm_region->creator_id;
    ptr += sizeof(uint64_t);
    char *send_q_start = (char *) shm_region->starting_addr + 2 * sizeof(uint64_t);
    if((uint64_t)send_q_start % CACHE_LINE_SIZE) {
        send_q_start += CACHE_LINE_SIZE - ((uint64_t)send_q_start % CACHE_LINE_SIZE);
    }
    *((uint64_t *) ptr) = (uint64_t) (send_q_start - (char *) shm_region->starting_addr);
    ptr += sizeof(uint64_t);
    char *recv_q_start = send_q_start + queue_size;
    if((uint64_t)recv_q_start % CACHE_LINE_SIZE) {
        recv_q_start += CACHE_LINE_SIZE - ((uint64_t)recv_q_start % CACHE_LINE_SIZE);
    }
    *((uint64_t *) ptr) = (uint64_t) (recv_q_start - (char *) shm_region->starting_addr);
    
    printf("Sender: pid: %d. send q start %lu. recv queue start %lu\n",
        shm_region->creator_id, 
        send_q_start - (char *) shm_region->starting_addr, 
        recv_q_start - (char *) shm_region->starting_addr
        );

    // send queue is for this process to send data to target process
    df_queue_t send_q = df_create_queue (send_q_start, num_slots, max_payload_size);
    df_queue_ep_t send_ep = df_get_queue_sender_ep(send_q);

    // recv queue is for this process to receive data sent by target process
    df_queue_t recv_q = df_create_queue (recv_q_start, num_slots, max_payload_size);
    df_queue_ep_t recv_ep = df_get_queue_receiver_ep(recv_q);

    // generate shm region contact info
    int contact_length;
    void *contact_info;
    contact_info = df_shm_region_contact_info(df_shm_handle, shm_region, &contact_length);
    if(!contact_info) {
        fprintf(stderr, "Cannot create contact info for shm region. %s:%d\n",
            __FILE__, __LINE__);
        exit(-1);
    }

    // send the contact info to receiver side through external mechanism
    // in this case, we use MPI
    int sender_pid = getpid();
    MPI_Send(&contact_length, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
    MPI_Send(contact_info, contact_length, MPI_BYTE, 1, 0, MPI_COMM_WORLD);
    MPI_Send(&sender_pid, 1, MPI_INT, 1, 1, MPI_COMM_WORLD);
    MPI_Send(&region_size, 1, MPI_INT, 1, 2, MPI_COMM_WORLD);

    // wait for receiver to attach region 
    MPI_Barrier(MPI_COMM_WORLD);

    // send messages to receiver from shm queue
    char send_buf[msg_size];
    memset(send_buf, 'a', msg_size);
    uint32_t i;
    for(i = 0; i < num_msgs; i ++) {
        if(df_enqueue(send_ep, send_buf, msg_size) != 0) {
            fprintf(stderr, "Sender: Error in enqueue. %s:%d\n", __FILE__, __LINE__);
            exit(-1);
        }
    }
    fprintf(stderr, "Sender sent %lu messges.\n", num_msgs);

    // receive messages from receiver
    char recv_buf[msg_size];
    memset(recv_buf, 'b', msg_size);
    void *recv_msg;
    size_t recv_length;
    for(i = 0; i < num_msgs; i ++) {
        if(df_dequeue(recv_ep, &recv_msg, &recv_length) != 0) {
            fprintf(stderr, "Sender: Error in dequeue. %s:%d\n", __FILE__, __LINE__);
            exit(-1);
        }
        if(recv_length != msg_size || memcmp(recv_msg, recv_buf, recv_length)) {
            fprintf(stderr, "Sender: Error message doesn't match. %s:%d\n", __FILE__, __LINE__);
            exit(-1);
        }
        df_release(recv_ep);
    }
    fprintf(stderr, "Sender received %lu messges.\n", num_msgs);

    // sync with receiver so both are done with data exchange
    // wait for receiver to detach
    MPI_Barrier(MPI_COMM_WORLD);

    // destroy queue
    df_destroy_ep(send_ep);
    df_destroy_queue(send_q);
    df_destroy_ep(recv_ep);
    df_destroy_queue(recv_q);

    // destroy the shm region
    if(df_destroy_shm_region(shm_region) != 0) {
        fprintf(stderr, "Cannot destory shm region. %s:%d\n",
            __FILE__, __LINE__);
        exit(-1);
    }
    free(contact_info);
    df_shm_finalize(df_shm_handle);
    return;
}


void receiver()
{
    // choose the underlying shm method
    df_shm_method_t df_shm_handle = df_shm_init(shm_method, NULL);
    if(!df_shm_handle) {
        fprintf(stderr, "Cannot initialize shm method %d. %s:%d\n",
            shm_method, __FILE__, __LINE__);
        exit(-1);
    }

    // wait for sender to tell me the contact info of shm region
    MPI_Status status;
    int contact_length;
    void *contact_info;
    pid_t creator_pid;
    int region_size;
    int rc = MPI_Recv(&contact_length, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);
    if(rc != MPI_SUCCESS) {
        fprintf(stderr, "MPI_Recv error: %d. %s:%d\n", rc, __FILE__, __LINE__);
        exit(-1); 
    }
    contact_info = malloc(contact_length);
    if(!contact_info) {
        fprintf(stderr, "Cannot allocate memory. %s:%d\n", __FILE__, __LINE__);
        exit(-1);
    }
    rc = MPI_Recv(contact_info, contact_length, MPI_BYTE, 0, 0, MPI_COMM_WORLD, &status);
    if(rc != MPI_SUCCESS) {
        fprintf(stderr, "MPI_Recv error: %d. %s:%d\n", rc, __FILE__, __LINE__);
        exit(-1);
    }
    rc = MPI_Recv(&creator_pid, 1, MPI_INT, 0, 1, MPI_COMM_WORLD, &status);
    if(rc != MPI_SUCCESS) {
        fprintf(stderr, "MPI_Recv error: %d. %s:%d\n", rc, __FILE__, __LINE__);
        exit(-1);
    }
    rc = MPI_Recv(&region_size, 1, MPI_INT, 0, 2, MPI_COMM_WORLD, &status);
    if(rc != MPI_SUCCESS) {
        fprintf(stderr, "MPI_Recv error: %d. %s:%d\n", rc, __FILE__, __LINE__);
        exit(-1);
    }

    // attach the region
    df_shm_region_t shm_region = df_attach_shm_region (df_shm_handle, creator_pid,
        contact_info, region_size, NULL);
    if(!shm_region) {
        fprintf(stderr, "Cannot attach shm region. %s:%d\n", __FILE__, __LINE__);
        exit(-1);
    }
       
    // locate queues in shm region
    uint64_t *sender_pid = (uint64_t *) shm_region->starting_addr;
    uint64_t *send_q_start = ((uint64_t *) shm_region->starting_addr + 1);
    uint64_t *recv_q_start = ((uint64_t *) shm_region->starting_addr + 2);

    printf("Receiver got sender's pid: %lu. send q start %lu. recv queue start %lu\n", 
        *sender_pid, *send_q_start, *recv_q_start);

    // setup local queue endpoints
    // Note: the receiver is receving end of send queue and sending end of recv queue
    df_queue_t send_q = (df_queue_t)((char *)shm_region->starting_addr + *send_q_start);
    df_queue_ep_t recv_ep = df_get_queue_receiver_ep(send_q);
    df_queue_t recv_q = (df_queue_t)((char *)shm_region->starting_addr + *recv_q_start);
    df_queue_ep_t send_ep = df_get_queue_sender_ep(recv_q);

    // tell sender that it's time to exchange data through queues
    MPI_Barrier(MPI_COMM_WORLD);

    // receive messages from sender
    char recv_buf[msg_size];
    memset(recv_buf, 'a', msg_size);
    void *recv_msg;
    size_t recv_length;
    uint32_t i;
    for(i = 0; i < num_msgs; i ++) {
        if(df_dequeue(recv_ep, &recv_msg, &recv_length) != 0) {
            fprintf(stderr, "Receiver: Error in dequeue. %s:%d\n", __FILE__, __LINE__);
            exit(-1);
        }
        if(recv_length != msg_size || memcmp(recv_msg, recv_buf, recv_length)) {
            fprintf(stderr, "Receiver: Error message doesn't match. %lu %s:%d\n", recv_length,__FILE__, __LINE__);
            exit(-1);
        }
        df_release(recv_ep);
    }
    fprintf(stderr, "Receiver received %lu messges.\n", num_msgs);
    
    // send messages to sender
    char send_buf[msg_size];
    memset(send_buf, 'b', msg_size);
    for(i = 0; i < num_msgs; i ++) {
        if(df_enqueue(send_ep, send_buf, msg_size) != 0) {
            fprintf(stderr, "Receiver: Error in enqueue. %s:%d\n", __FILE__, __LINE__);
            exit(-1);
        }
    }
    fprintf(stderr, "Receiver sent %lu messges.\n", num_msgs);

    // cleanup queue eps
    df_destroy_ep(send_ep);
    df_destroy_ep(recv_ep);
    
    // detach the shm region
    if(df_detach_shm_region(shm_region) != 0) {
        fprintf(stderr, "Cannot detach shm region. %s:%d\n", __FILE__, __LINE__);
        exit(-1);
    }

    // tell sender we have detached the region
    MPI_Barrier(MPI_COMM_WORLD);
    free(contact_info);
    df_shm_finalize(df_shm_handle);
    return;
}

