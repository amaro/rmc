set(INCLUDE_DIR "${RMC_SOURCE_DIR}/src")

add_library(rdmapeer_obj OBJECT rdma/rdmapeer.cpp)
target_include_directories(rdmapeer_obj PUBLIC ${INCLUDE_DIR})

add_library(rdmaclient_obj OBJECT rdma/rdmaclient.cpp)
target_include_directories(rdmaclient_obj PUBLIC ${INCLUDE_DIR})

add_library(rdmaserver_obj OBJECT rdma/rdmaserver.cpp)
target_include_directories(rdmaserver_obj PUBLIC ${INCLUDE_DIR})

add_library(onesidedclient_obj OBJECT onesidedclient.cpp)
target_include_directories(onesidedclient_obj PUBLIC ${INCLUDE_DIR})

# rmc client
add_executable(client client.cpp
    #${INCLUDE_DIR}/utils/utils.h
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_include_directories(client PUBLIC ${INCLUDE_DIR})
target_link_libraries(client rdmacm ibverbs pthread)

# hostserver
add_executable(hostserver hostserver.cpp
    #${INCLUDE_DIR}/utils/utils.h
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_include_directories(hostserver PUBLIC ${INCLUDE_DIR})
target_link_libraries(hostserver rdmacm ibverbs pthread)

# nicserver; needs both rdmaserver and rdmaclient;
# and for some reason it needs onesidedclient.cpp too (TODO)
add_executable(nicserver nicserver.cpp
    #${INCLUDE_DIR}/utils/utils.h
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmaserver_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_include_directories(nicserver PUBLIC ${INCLUDE_DIR})
target_link_libraries(nicserver rdmacm ibverbs pthread)

# no rmc client; i.e., a one sided client that connects to host server directly
# without a nicserver
add_executable(normc_client normc_client.cpp
    #${INCLUDE_DIR}/utils/utils.h
    $<TARGET_OBJECTS:onesidedclient_obj>
    $<TARGET_OBJECTS:rdmaclient_obj>
    $<TARGET_OBJECTS:rdmapeer_obj>)
target_include_directories(normc_client PUBLIC ${INCLUDE_DIR})
target_link_libraries(normc_client rdmacm ibverbs pthread)
