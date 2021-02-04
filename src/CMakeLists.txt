set(INCLUDE_DIR "${RMC_SOURCE_DIR}/src")
include_directories(${INCLUDE_DIR})

add_library(rdmapeer_obj OBJECT rdma/rdmapeer.cpp)

add_library(rdmaclient_obj OBJECT rdma/rdmaclient.cpp)

add_library(rdmaserver_obj OBJECT rdma/rdmaserver.cpp)

add_library(onesidedclient_obj OBJECT onesidedclient.cpp)
target_compile_options(onesidedclient_obj PUBLIC "-fcoroutines")

add_library(scheduler_obj OBJECT scheduler.cpp)
target_compile_options(scheduler_obj PUBLIC "-fcoroutines")

# rmc client
add_executable(client client.cpp
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_link_libraries(client rdmacm ibverbs pthread)

# hostserver
add_executable(hostserver hostserver.cpp
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_link_libraries(hostserver rdmacm ibverbs pthread)

# nicserver; needs both rdmaserver and rdmaclient;
# and for some reason it needs onesidedclient.cpp too (TODO)
add_executable(nicserver nicserver.cpp
    $<TARGET_OBJECTS:scheduler_obj>
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_compile_options(nicserver PUBLIC "-fcoroutines")
target_link_libraries(nicserver rdmacm ibverbs pthread)

# no rmc client; i.e., a one sided client that connects to host server directly
# without a nicserver
add_executable(normc_client normc_client.cpp
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_compile_options(normc_client PUBLIC "-fcoroutines")
target_link_libraries(normc_client rdmacm ibverbs pthread)
