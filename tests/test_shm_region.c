/*
 * This test program excercises DF's shm region routines.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <mpi.h>
#include "df_shm.h"

// test parameters
enum DF_SHM_METHOD shm_method = DF_SHM_METHOD_SYSV;
size_t region_size = 4096;


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
    
    // create a shm region
    df_shm_region_t region = df_create_shm_region(df_shm_handle, region_size, NULL);
    if(!region) {
        fprintf(stderr, "Cannot create shm region. %s:%d\n", 
            __FILE__, __LINE__);
        exit(-1);
    }
    
    // generate shm region contact info
    int contact_length;
    void *contact_info;
    contact_info = df_shm_region_contact_info(df_shm_handle, region, &contact_length);
    if(!contact_info) {
        fprintf(stderr, "Cannot create contact info for shm region. %s:%d\n",
            __FILE__, __LINE__);
        exit(-1);
    }

    // initialize the region
    char *region_start = (char *) region->starting_addr;
    memset(region_start, 0, region_size);

    // write down sender's pid into shm region
    int *sender_pid = (int *) region_start;
    *sender_pid = getpid();
    int *receiver_pid = sender_pid + 1;
    *receiver_pid = -1;
    printf("Sender's pid: %d\n", *sender_pid);

    // send the contact info to receiver side through external mechanism
    // in this case, we use MPI
    MPI_Send(&contact_length, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
    MPI_Send(contact_info, contact_length, MPI_BYTE, 1, 0, MPI_COMM_WORLD);
    MPI_Send(&sender_pid, 1, MPI_INT, 1, 1, MPI_COMM_WORLD);

    // wait for receiver to attach region and write something into it
    MPI_Barrier(MPI_COMM_WORLD);

    if(*receiver_pid == -1) { 
        fprintf(stderr, "Cannot read receiver's pid. %s:%d\n",
            __FILE__, __LINE__);
        exit(-1);
    }
    printf("Sender got receiver's pid: %d\n", *receiver_pid);

    // wait for receiver to detach
    MPI_Barrier(MPI_COMM_WORLD);

    // destroy the shm region
    if(df_destroy_shm_region(region) != 0) {
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

    // attach the region
    df_shm_region_t region = df_attach_shm_region (df_shm_handle, creator_pid,
        contact_info, region_size, NULL);
    if(!region) {
        fprintf(stderr, "Cannot attach shm region. %s:%d\n", __FILE__, __LINE__);
        exit(-1);
    }
       
    // read from shm region and write something back
    int *sender_pid = (int *) region->starting_addr;
    int *receiver_pid = sender_pid + 1;
    *receiver_pid = getpid();
    printf("Receiver's pid: %d\n", *receiver_pid);
    printf("Receiver got sender's pid: %d.\n", *sender_pid);

    // tell sender to check what the receiver just wrote in shm region
    MPI_Barrier(MPI_COMM_WORLD);

    // detach the shm region
    if(df_detach_shm_region(region) != 0) {
        fprintf(stderr, "Cannot detach shm region. %s:%d\n", __FILE__, __LINE__);
        exit(-1);
    }

    // tell sender we have detached the region
    MPI_Barrier(MPI_COMM_WORLD);
    free(contact_info);
    df_shm_finalize(df_shm_handle);
    return;
}

