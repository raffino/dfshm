SMOKY=n
JAGUAR=n
ROHAN=y

ifeq ($(JAGUAR),y)
    #use this for cray compute nodes
    CC=cc -g -target=linux
    INSTALL_PREFIX=$(HOME)/work/pe.pgi
endif

ifeq ($(SMOKY),y)
    CC=mpicc -g
    INSTALL_PREFIX=$(HOME)/work/smoky
endif

ifeq ($(ROHAN),y)
    CC=mpicc -g -DNDEBUG=1
    LD_FLAGS=-lrt
    INSTALL_PREFIX=$(HOME)/work/rohan
endif

OBJs=test_shm_region.o test_queue_sendrecv.o perf_queue_latency.o

all: test_shm_region test_queue_sendrecv perf_queue_latency

test_shm_region: test_shm_region.o
	$(CC) $^ ../libdf_shm.a $(LD_FLAGS) -o $@

test_queue_sendrecv: test_queue_sendrecv.o
	$(CC) $^ ../libdf_shm.a $(LD_FLAGS) -o $@

perf_queue_latency: perf_queue_latency.o
	$(CC) $^ ../libdf_shm.a $(LD_FLAGS) -o $@

.c.o :
	$(CC) -c $(I_PATH) -I.. $<

clean:
	rm -rf test_shm_region
	rm -rf test_queue_sendrecv
	rm -rf perf_queue_latency
	rm -f *.o 


