set(SRC_DIR "${RMC_SOURCE_DIR}/src")
include_directories(${SRC_DIR})
include_directories(${SRC_DIR}/lib)

add_library(rdmapeer_obj OBJECT rdma/rdmapeer.cpp)
add_library(rdmaclient_obj OBJECT rdma/rdmaclient.cpp)
add_library(rdmaserver_obj OBJECT rdma/rdmaserver.cpp)
add_library(onesidedclient_obj OBJECT onesidedclient.cpp)
add_library(rmcs_obj OBJECT rmcs.cpp)

add_library(scheduler_rdma_obj OBJECT scheduler.cpp)
target_compile_definitions(scheduler_rdma_obj PUBLIC -DBACKEND_RDMA=1)

add_library(scheduler_rdmacomp_obj OBJECT scheduler.cpp)
target_compile_definitions(scheduler_rdmacomp_obj PUBLIC -DBACKEND_RDMA_COMP=1)

add_library(scheduler_dram_obj OBJECT scheduler.cpp)
target_compile_definitions(scheduler_dram_obj PUBLIC -DBACKEND_DRAM=1)

add_library(scheduler_dramcomp_obj OBJECT scheduler.cpp)
target_compile_definitions(scheduler_dramcomp_obj PUBLIC -DBACKEND_DRAM_COMP=1)

#add_library(scheduler_rdma_client_obj OBJECT scheduler.cpp)
#target_compile_definitions(scheduler_rdma_client_obj PUBLIC -DBACKEND_RDMA=1 -DLOCATION_CLIENT=1)

#add_library(scheduler_rdma_hash_client_obj OBJECT scheduler.cpp)
#target_compile_definitions(scheduler_rdma_hash_client_obj PUBLIC -DBACKEND_RDMA=1 -DLOCATION_CLIENT=1 -DWORKLOAD_HASHTABLE=1)

# rmc client
add_executable(client client.cpp
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_compile_options(client PUBLIC "-fexceptions") # for cxxopts (TODO)
target_compile_options(client PUBLIC "-frtti") # for cxxopts (TODO)
target_link_libraries(client mlx5 rdmacm ibverbs pthread)

# hostserver
add_executable(hostserver hostserver.cpp
    $<TARGET_OBJECTS:rmcs_obj>
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_link_libraries(hostserver mlx5 rdmacm ibverbs pthread)

# nicserver
add_executable(nicserver nicserver.cpp
    $<TARGET_OBJECTS:rmcs_obj>
    $<TARGET_OBJECTS:scheduler_rdma_obj>
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_compile_definitions(nicserver PUBLIC -DBACKEND_RDMA=1)
target_link_libraries(nicserver mlx5 rdmacm ibverbs pthread)

add_executable(nicserver_rdmacomp nicserver.cpp
    $<TARGET_OBJECTS:rmcs_obj>
    $<TARGET_OBJECTS:scheduler_rdmacomp_obj>
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_compile_definitions(nicserver_rdmacomp PUBLIC -DBACKEND_RDMA_COMP=1)
target_link_libraries(nicserver_rdmacomp mlx5 rdmacm ibverbs pthread)

add_executable(nicserver_dram nicserver.cpp
    $<TARGET_OBJECTS:rmcs_obj>
    $<TARGET_OBJECTS:scheduler_dram_obj>
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_compile_definitions(nicserver_dram PUBLIC -DBACKEND_DRAM=1)
target_link_libraries(nicserver_dram mlx5 rdmacm ibverbs pthread)

add_executable(nicserver_dramcomp nicserver.cpp
    $<TARGET_OBJECTS:rmcs_obj>
    $<TARGET_OBJECTS:scheduler_dramcomp_obj>
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_compile_definitions(nicserver_dramcomp PUBLIC -DBACKEND_DRAM_COMP=1)
target_link_libraries(nicserver_dramcomp mlx5 rdmacm ibverbs pthread)

#add_executable(nicserver_client nicserver.cpp
#    $<TARGET_OBJECTS:scheduler_rdma_client_obj>
#    $<TARGET_OBJECTS:onesidedclient_obj>
#    $<TARGET_OBJECTS:rdmaclient_obj>
#    $<TARGET_OBJECTS:rdmaserver_obj>
#    $<TARGET_OBJECTS:rdmapeer_obj>)
#target_compile_definitions(nicserver_client PUBLIC -DBACKEND_RDMA=1 -DLOCATION_CLIENT=1)
#target_link_libraries(nicserver_client mlx5 rdmacm ibverbs pthread)
