#!/bin/sh

hostname >myhostfile

ulimit -c unlimited

# Test 1: shared memroy region test
echo "================= Run Test 1 ==================="
echo " shared memroy region test"
echo "================================================"
mpirun -np 2 -hostfile ./myhostfile ./test_shm_region
echo
if [ $? -eq 0 ]
then
    echo "Test 1 Passed"
else
    echo "Test 1 Failed"
fi
echo "================================================"

# Test 2: shared memory queue test
echo
echo "================= Run Test 2 ==================="
echo " shared memroy queue test"
echo "================================================"
mpirun -np 2 -hostfile ./myhostfile ./test_queue_sendrecv
echo
if [ $? -eq 0 ]
then
    echo "Test 2 Passed"
else
    echo "Test 2 Failed"
fi
echo "================================================"

# Test 3: shared memory queue latency benchmark
echo
echo "================= Run Test 3 ==================="
echo " shared memroy queue latency benchmark"
echo "================================================"
echo
echo " latency result with SystemV shm"
echo
mpirun -np 2 -hostfile ./myhostfile ./perf_queue_latency S 2>/dev/null
echo
echo
echo " latency result with mmap shm"
echo
mpirun -np 2 -hostfile ./myhostfile ./perf_queue_latency M 2>/dev/null
echo
echo
echo " latency result with POSIX shm"
echo
mpirun -np 2 -hostfile ./myhostfile ./perf_queue_latency P 2>/dev/null
echo
if [ $? -eq 0 ]
then
    echo "Test 3 Passed"
else
    echo "Test 3 Failed"
fi
echo "================================================"


# cleanup
rm -rf myhostfile
