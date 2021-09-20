CXX_x86:=g++-10
CXX_arm:=/home/amaro/RMC/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-g++

INCLUDE:=-I../../src/
CXXFLAGS_x86:=-march=native
CXXFLAGS_arm:=-march=armv8-a+simd+crc -mtune=cortex-a72
CXXFLAGS:=-O3 $(INCLUDE) -std=c++20 -Wall -Werror

HEADERS_BOOST_arm:=-I/home/amaro/RMC/prefix/include -Wno-error=parentheses
LIB_BOOST_arm:=/home/amaro/RMC/prefix/lib

all: threads coroboost func rmcs

.PHONY: threads
threads: threads_x86 threads_arm

threads_x86: threads.cpp
	$(CXX_x86) $(CXXFLAGS) $(CXXFLAGS_x86) $< -o $@ -lpthread

threads_arm: threads.cpp
	$(CXX_arm) $(CXXFLAGS) $(CXXFLAGS_arm) $< -o $@ -lpthread

.PHONY: coroboost
coroboost: coroboost_x86 coroboost_arm coroboostv2_x86 coroboostv2_arm

coroboost_x86: coroboost.cpp
	$(CXX_x86) $(CXXFLAGS) $(CXXFLAGS_x86) $< -o $@ -lpthread -lboost_coroutine -lboost_context -lboost_system

coroboost_arm: coroboost.cpp
	$(CXX_arm) $(CXXFLAGS) $(CXXFLAGS_arm) $(HEADERS_BOOST_arm) $< -o $@ -L$(LIB_BOOST_arm) -lboost_context -lboost_coroutine -lboost_thread -lboost_chrono -lboost_system

coroboostv2_x86: coroboostv2.cpp
	$(CXX_x86) $(CXXFLAGS) $(CXXFLAGS_x86) $< -o $@ -lpthread -lboost_coroutine -lboost_context

coroboostv2_arm: coroboostv2.cpp
	$(CXX_arm) $(CXXFLAGS) $(CXXFLAGS_arm) $(HEADERS_BOOST_arm) $< -o $@ -L$(LIB_BOOST_arm) -lboost_context -lboost_coroutine -lboost_thread -lboost_chrono -lboost_system

.PHONY: func
func: func_x86 func_arm

func_x86: func.cpp
	$(CXX_x86) $(CXXFLAGS) $(CXXFLAGS_x86) $< -o $@

func_arm: func.cpp
	$(CXX_arm) $(CXXFLAGS) $(CXXFLAGS_arm) $< -o $@

.PHONY: rmcs
rmcs: rmcs_x86 rmcs_arm

rmcs_x86: rmcs.cpp
	$(CXX_x86) $(CXXFLAGS) $(CXXFLAGS_x86) -fcoroutines $< -o $@

rmcs_arm: rmcs.cpp
	$(CXX_arm) $(CXXFLAGS) $(CXXFLAGS_arm) -fcoroutines $< -o $@

.PHONY: clean
clean:
	rm -rf threads_* coroboost_* coroboostv2_* func_* rmcs_*
