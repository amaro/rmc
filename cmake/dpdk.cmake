
set(MSG_PRE "[dpdk]")

if (DEFINED ENV{RTE_SDK})
    set(RTE_SDK $ENV{RTE_SDK})
else()
    set(RTE_SDK "$ENV{HOME}/opt/dpdk-19.11")
endif()

if (DEFINED ENV{RTE_TARGET})
    set(RTE_TARGET $ENV{RTE_TARGET})
else()
    set(RTE_TARGET x86_64-native-linuxapp-gcc)
endif()

file(STRINGS "${RTE_SDK}/VERSION" RTE_VERSION)
message(STATUS "${MSG_PRE} Using RTE_SDK=${RTE_SDK}")
message(STATUS "${MSG_PRE} Using RTE_TARGET=${RTE_TARGET}")
message(STATUS "${MSG_PRE} Detected DPDK version: ${RTE_VERSION}")


#set(RTE_LIBRARY_HANDLES eal hash kvargs mbuf mempool net rcu ring)
set(RTE_LIBRARY_HANDLES eal ring hash)

set(DPDK_LIB "-Wl,--whole-archive")
set(DPDK_INCLUDE ${RTE_SDK}/include/dpdk)

foreach(handle ${RTE_LIBRARY_HANDLES})
    find_library(LIB_DPDK_RTE_${handle} rte_${handle} HINTS ${RTE_SDK}/lib)
    if(NOT LIB_DPDK_RTE_${handle})
        message(FATAL_ERROR "${MSG_PRE} librte_${handle} not found")
    endif()
    list(APPEND DPDK_LIB "${LIB_DPDK_RTE_${handle}}")
    message(STATUS "${MSG_PRE} Found librte_${handle}: ${LIB_DPDK_RTE_${handle}}")
endforeach()

list(APPEND DPDK_LIB "-Wl,--no-whole-archive")

message(STATUS "${MSG_PRE} DPDK_INCLUDE and DPDK_LIB set")
