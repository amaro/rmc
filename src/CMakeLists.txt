set(INCLUDE_DIR "${RMC_SOURCE_DIR}/src")
include_directories(${INCLUDE_DIR})

add_library(rdmapeer_obj OBJECT rdma/rdmapeer.cpp)
target_compile_options(rdmapeer_obj PUBLIC "-fno-exceptions")
target_compile_options(rdmapeer_obj PUBLIC "-fno-rtti")

add_library(rdmaclient_obj OBJECT rdma/rdmaclient.cpp)
target_compile_options(rdmaclient_obj PUBLIC "-fno-exceptions")
target_compile_options(rdmaclient_obj PUBLIC "-fno-rtti")

add_library(rdmaserver_obj OBJECT rdma/rdmaserver.cpp)
target_compile_options(rdmaserver_obj PUBLIC "-fno-exceptions")
target_compile_options(rdmaserver_obj PUBLIC "-fno-rtti")

add_library(onesidedclient_obj OBJECT onesidedclient.cpp)
target_compile_options(onesidedclient_obj PUBLIC "-fcoroutines")
target_compile_options(onesidedclient_obj PUBLIC "-fno-exceptions")
target_compile_options(onesidedclient_obj PUBLIC "-fno-rtti")

add_library(scheduler_obj OBJECT scheduler.cpp)
target_compile_options(scheduler_obj PUBLIC "-fcoroutines")
target_compile_options(scheduler_obj PUBLIC "-fno-exceptions")
target_compile_options(scheduler_obj PUBLIC "-fno-rtti")

# rmc client
add_executable(client client.cpp
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_link_libraries(client mlx5 rdmacm ibverbs pthread)

# hostserver
add_executable(hostserver hostserver.cpp
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_link_libraries(hostserver mlx5 rdmacm ibverbs pthread)

# nicserver; needs both rdmaserver and rdmaclient;
# and for some reason it needs onesidedclient.cpp too (TODO)
add_executable(nicserver nicserver.cpp
    $<TARGET_OBJECTS:scheduler_obj>
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_compile_options(nicserver PUBLIC "-fcoroutines")
target_compile_options(nicserver PUBLIC "-fno-exceptions")
target_compile_options(nicserver PUBLIC "-fno-rtti")
target_link_libraries(nicserver mlx5 rdmacm ibverbs pthread)

# no rmc client; i.e., a one sided client that connects to host server directly
# without a nicserver
add_executable(normc_client normc_client.cpp
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_compile_options(normc_client PUBLIC "-fcoroutines")
target_link_libraries(normc_client mlx5 rdmacm ibverbs pthread)
