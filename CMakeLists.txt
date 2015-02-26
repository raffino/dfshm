cmake_minimum_required(VERSION 2.8.3)
cmake_policy(VERSION 2.8.3)
project(df_shm)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckIncludeFiles)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

IF( NOT CMAKE_BUILD_TYPE )
SET( CMAKE_BUILD_TYPE "RelWithDebInfo" )
ENDIF()

set (SRC_LIST df_shm.c df_shm_mmap.c df_shm_posixshm.c df_shm_sysv.c df_shm_queue.c)

add_library(df_shm SHARED ${SRC_LIST})
add_library(df_shm-static STATIC ${SRC_LIST})

SET_TARGET_PROPERTIES(df_shm-static PROPERTIES OUTPUT_NAME "df_shm")
SET_TARGET_PROPERTIES(df_shm-static PROPERTIES PREFIX "lib")
SET_TARGET_PROPERTIES(df_shm PROPERTIES STATIC_LIB "libdf_shm.a")
SET_TARGET_PROPERTIES(df_shm PROPERTIES LT_SHOULDNOTLINK "no")

target_link_libraries (df_shm-static ${CMAKE_THREAD_LIBS_INIT} ${LINK_LIBRARIES})
target_link_libraries (df_shm ${CMAKE_THREAD_LIBS_INIT} ${LINK_LIBRARIES})

# check availability of underlying shared memory methods
check_function_exists(mmap HAVE_MMAP)
check_function_exists(shmget HAVE_SYSV)
CHECK_LIBRARY_EXISTS(rt shm_open "" HAVE_POSIX_SHM)

execute_process(COMMAND getconf LEVEL1_DCACHE_LINESIZE OUTPUT_VARIABLE CACHE_LINE_SIZE ERROR_QUIET)
if ("${CACHE_LINE_SIZE}" STREQUAL "")
execute_process(COMMAND sysctl -a hw.cachelinesize COMMAND sed "s/.* //g" OUTPUT_VARIABLE CACHE_LINE_SIZE)
endif ("${CACHE_LINE_SIZE}" STREQUAL "")
execute_process(COMMAND getconf PAGESIZE OUTPUT_VARIABLE PAGE_SIZE)

CONFIGURE_FILE( ${CMAKE_CURRENT_SOURCE_DIR}/df_config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/df_config.h )

if (${CMAKE_C_COMPILER_ID} MATCHES "Intel")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -shared-intel")
endif()

INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/df_config.h DESTINATION include)
INSTALL(FILES df_shm.h DESTINATION include)
INSTALL(FILES df_shm_queue.h DESTINATION include)
INSTALL(TARGETS df_shm df_shm-static
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

# display status message for important variables
MESSAGE( STATUS )
MESSAGE( STATUS "-------------------------------------------------------------------------------" )
MESSAGE( STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}" )
MESSAGE( STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}" )
MESSAGE( STATUS "Change a value with: cmake -D<Variable>=<Value>" )
MESSAGE( STATUS "-------------------------------------------------------------------------------" )
MESSAGE( STATUS )

