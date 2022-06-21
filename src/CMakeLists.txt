set(SRC_DIR "${RMC_SOURCE_DIR}/src")
include_directories(${SRC_DIR})
include_directories(${SRC_DIR}/lib)

add_library(rdmapeer_obj OBJECT rdma/rdmapeer.cpp)
# this shouldn't be needed but the current code mess makes it necessary
target_compile_options(rdmapeer_obj PUBLIC "-fcoroutines")
target_compile_options(rdmapeer_obj PUBLIC "-fno-exceptions")
target_compile_options(rdmapeer_obj PUBLIC "-fno-rtti")

add_library(rdmaclient_obj OBJECT rdma/rdmaclient.cpp)
# this shouldn't be needed but the current code mess makes it necessary
target_compile_options(rdmaclient_obj PUBLIC "-fcoroutines")
target_compile_options(rdmaclient_obj PUBLIC "-fno-exceptions")
target_compile_options(rdmaclient_obj PUBLIC "-fno-rtti")

add_library(rdmaserver_obj OBJECT rdma/rdmaserver.cpp)
# this shouldn't be needed but the current code mess makes it necessary
target_compile_options(rdmaserver_obj PUBLIC "-fcoroutines")
target_compile_options(rdmaserver_obj PUBLIC "-fno-exceptions")
target_compile_options(rdmaserver_obj PUBLIC "-fno-rtti")

add_library(onesidedclient_obj OBJECT onesidedclient.cpp)
target_compile_options(onesidedclient_obj PUBLIC "-fcoroutines")
target_compile_options(onesidedclient_obj PUBLIC "-fno-exceptions")
target_compile_options(onesidedclient_obj PUBLIC "-fno-rtti")

add_library(scheduler_rdma_obj OBJECT scheduler.cpp)
target_compile_options(scheduler_rdma_obj PUBLIC "-fcoroutines")
target_compile_options(scheduler_rdma_obj PUBLIC "-fno-exceptions")
target_compile_options(scheduler_rdma_obj PUBLIC "-fno-rtti")
target_compile_definitions(scheduler_rdma_obj PUBLIC -DBACKEND_RDMA=1)

#add_library(scheduler_rdma_hash_obj OBJECT scheduler.cpp)
#target_compile_options(scheduler_rdma_hash_obj PUBLIC "-fcoroutines")
#target_compile_options(scheduler_rdma_hash_obj PUBLIC "-fno-exceptions")
#target_compile_options(scheduler_rdma_hash_obj PUBLIC "-fno-rtti")
#target_compile_definitions(scheduler_rdma_hash_obj PUBLIC -DBACKEND_RDMA=1 -DWORKLOAD_HASHTABLE=1)
#
add_library(scheduler_rdmacomp_obj OBJECT scheduler.cpp)
target_compile_options(scheduler_rdmacomp_obj PUBLIC "-fcoroutines")
target_compile_options(scheduler_rdmacomp_obj PUBLIC "-fno-exceptions")
target_compile_options(scheduler_rdmacomp_obj PUBLIC "-fno-rtti")
target_compile_definitions(scheduler_rdmacomp_obj PUBLIC -DBACKEND_RDMA_COMP=1)
#
#add_library(scheduler_dram_obj OBJECT scheduler.cpp)
#target_compile_options(scheduler_dram_obj PUBLIC "-fcoroutines")
#target_compile_options(scheduler_dram_obj PUBLIC "-fno-exceptions")
#target_compile_options(scheduler_dram_obj PUBLIC "-fno-rtti")
#target_compile_definitions(scheduler_dram_obj PUBLIC -DBACKEND_DRAM=1)
#
#add_library(scheduler_dram_hash_obj OBJECT scheduler.cpp)
#target_compile_options(scheduler_dram_hash_obj PUBLIC "-fcoroutines")
#target_compile_options(scheduler_dram_hash_obj PUBLIC "-fno-exceptions")
#target_compile_options(scheduler_dram_hash_obj PUBLIC "-fno-rtti")
#target_compile_definitions(scheduler_dram_hash_obj PUBLIC -DBACKEND_DRAM=1 -DWORKLOAD_HASHTABLE=1)
#
#add_library(scheduler_rdma_client_obj OBJECT scheduler.cpp)
#target_compile_options(scheduler_rdma_client_obj PUBLIC "-fcoroutines")
#target_compile_options(scheduler_rdma_client_obj PUBLIC "-fno-exceptions")
#target_compile_options(scheduler_rdma_client_obj PUBLIC "-fno-rtti")
#target_compile_definitions(scheduler_rdma_client_obj PUBLIC -DBACKEND_RDMA=1 -DLOCATION_CLIENT=1)
#
#add_library(scheduler_rdma_hash_client_obj OBJECT scheduler.cpp)
#target_compile_options(scheduler_rdma_hash_client_obj PUBLIC "-fcoroutines")
#target_compile_options(scheduler_rdma_hash_client_obj PUBLIC "-fno-exceptions")
#target_compile_options(scheduler_rdma_hash_client_obj PUBLIC "-fno-rtti")
#target_compile_definitions(scheduler_rdma_hash_client_obj PUBLIC -DBACKEND_RDMA=1 -DLOCATION_CLIENT=1 -DWORKLOAD_HASHTABLE=1)
#
#add_library(cuckoo_hash_obj OBJECT lib/cuckoo_hash.cpp lib/lookup3.cpp)
#target_compile_options(cuckoo_hash_obj PUBLIC "-fcoroutines")
#target_compile_options(cuckoo_hash_obj PUBLIC "-fno-exceptions")
#target_compile_options(cuckoo_hash_obj PUBLIC "-fno-rtti")
#target_compile_definitions(cuckoo_hash_obj PUBLIC)

# rmc client
add_executable(client client.cpp
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_compile_options(client PUBLIC "-fcoroutines")
target_link_libraries(client mlx5 rdmacm ibverbs pthread)

# hostserver
add_executable(hostserver hostserver.cpp
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_compile_options(hostserver PUBLIC "-fcoroutines")
target_link_libraries(hostserver mlx5 rdmacm ibverbs pthread)

# nicserver; needs both rdmaserver and rdmaclient;
# and for some reason it needs onesidedclient.cpp too (TODO)
add_executable(nicserver nicserver.cpp
    $<TARGET_OBJECTS:scheduler_rdma_obj>
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_compile_options(nicserver PUBLIC "-fcoroutines")
target_compile_options(nicserver PUBLIC "-fno-exceptions")
target_compile_options(nicserver PUBLIC "-fno-rtti")
target_compile_definitions(nicserver PUBLIC -DBACKEND_RDMA=1)
target_link_libraries(nicserver mlx5 rdmacm ibverbs pthread)

#add_executable(nicserver_rdma_hash nicserver.cpp
#    $<TARGET_OBJECTS:scheduler_rdma_hash_obj>
#    $<TARGET_OBJECTS:onesidedclient_obj>
#    $<TARGET_OBJECTS:rdmaclient_obj>
#    $<TARGET_OBJECTS:rdmaserver_obj>
#    $<TARGET_OBJECTS:rdmapeer_obj>
#    $<TARGET_OBJECTS:cuckoo_hash_obj>)
#target_compile_options(nicserver_rdma_hash PUBLIC "-fcoroutines")
#target_compile_options(nicserver_rdma_hash PUBLIC "-fno-exceptions")
#target_compile_options(nicserver_rdma_hash PUBLIC "-fno-rtti")
#target_compile_definitions(nicserver_rdma_hash PUBLIC -DBACKEND_RDMA=1 -DWORKLOAD_HASHTABLE=1)
#target_link_libraries(nicserver_rdma_hash mlx5 rdmacm ibverbs pthread)
#
add_executable(nicserver_rdmacomp nicserver.cpp
    $<TARGET_OBJECTS:scheduler_rdmacomp_obj>
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_compile_options(nicserver_rdmacomp PUBLIC "-fcoroutines")
target_compile_options(nicserver_rdmacomp PUBLIC "-fno-exceptions")
target_compile_options(nicserver_rdmacomp PUBLIC "-fno-rtti")
target_compile_definitions(nicserver_rdmacomp PUBLIC -DBACKEND_RDMA_COMP=1)
target_link_libraries(nicserver_rdmacomp mlx5 rdmacm ibverbs pthread)
#
#add_executable(nicserver_dram nicserver.cpp
#    $<TARGET_OBJECTS:scheduler_dram_obj>
#    $<TARGET_OBJECTS:onesidedclient_obj>
#    $<TARGET_OBJECTS:rdmaclient_obj>
#    $<TARGET_OBJECTS:rdmaserver_obj>
#    $<TARGET_OBJECTS:rdmapeer_obj>)
#target_compile_options(nicserver_dram PUBLIC "-fcoroutines")
#target_compile_options(nicserver_dram PUBLIC "-fno-exceptions")
#target_compile_options(nicserver_dram PUBLIC "-fno-rtti")
#target_compile_definitions(nicserver_dram PUBLIC -DBACKEND_DRAM=1)
#target_link_libraries(nicserver_dram mlx5 rdmacm ibverbs pthread)
#
#add_executable(nicserver_dram_hash nicserver.cpp
#    $<TARGET_OBJECTS:scheduler_dram_hash_obj>
#    $<TARGET_OBJECTS:onesidedclient_obj>
#    $<TARGET_OBJECTS:rdmaclient_obj>
#    $<TARGET_OBJECTS:rdmaserver_obj>
#    $<TARGET_OBJECTS:rdmapeer_obj>
#    $<TARGET_OBJECTS:cuckoo_hash_obj>)
#target_compile_options(nicserver_dram_hash PUBLIC "-fcoroutines")
#target_compile_options(nicserver_dram_hash PUBLIC "-fno-exceptions")
#target_compile_options(nicserver_dram_hash PUBLIC "-fno-rtti")
#target_compile_definitions(nicserver_dram_hash PUBLIC -DBACKEND_DRAM=1 -DWORKLOAD_HASHTABLE=1)
#target_link_libraries(nicserver_dram_hash mlx5 rdmacm ibverbs pthread)
#
#add_executable(nicserver_client nicserver.cpp
#    $<TARGET_OBJECTS:scheduler_rdma_client_obj>
#    $<TARGET_OBJECTS:onesidedclient_obj>
#    $<TARGET_OBJECTS:rdmaclient_obj>
#    $<TARGET_OBJECTS:rdmaserver_obj>
#    $<TARGET_OBJECTS:rdmapeer_obj>)
#target_compile_options(nicserver_client PUBLIC "-fcoroutines")
#target_compile_options(nicserver_client PUBLIC "-fno-exceptions")
#target_compile_options(nicserver_client PUBLIC "-fno-rtti")
#target_compile_definitions(nicserver_client PUBLIC -DBACKEND_RDMA=1 -DLOCATION_CLIENT=1)
#target_link_libraries(nicserver_client mlx5 rdmacm ibverbs pthread)
#
#add_executable(nicserver_client_hash nicserver.cpp
#    $<TARGET_OBJECTS:scheduler_rdma_hash_client_obj>
#    $<TARGET_OBJECTS:onesidedclient_obj>
#    $<TARGET_OBJECTS:rdmaclient_obj>
#    $<TARGET_OBJECTS:rdmaserver_obj>
#    $<TARGET_OBJECTS:rdmapeer_obj>
#    $<TARGET_OBJECTS:cuckoo_hash_obj>)
#target_compile_options(nicserver_client_hash PUBLIC "-fcoroutines")
#target_compile_options(nicserver_client_hash PUBLIC "-fno-exceptions")
#target_compile_options(nicserver_client_hash PUBLIC "-fno-rtti")
#target_compile_definitions(nicserver_client_hash PUBLIC -DBACKEND_RDMA=1 -DLOCATION_CLIENT=1 -DWORKLOAD_HASHTABLE=1)
#target_link_libraries(nicserver_client_hash mlx5 rdmacm ibverbs pthread)
