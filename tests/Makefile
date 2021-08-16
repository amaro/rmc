CXX_x86:=g++-10
CXX_arm:=/home/amaro/RMC/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-g++

CXXFLAGS_x86:=-march=native
CXXFLAGS_arm:=-march=armv8-a+simd+crc -mtune=cortex-a72
CXXFLAGS:=-O3

all: threads

.PHONY: threads
threads: threads_x86 threads_arm

threads_x86: threads.cpp
	$(CXX_x86) $(CXXFLAGS) $(CXXFLAGS_x86) threads.cpp -o $@ -lpthread

threads_arm: threads.cpp
	$(CXX_arm) $(CXXFLAGS) $(CXXFLAGS_arm) threads.cpp -o $@ -lpthread

.PHONY: clean
clean:
	rm -rf threads_x86 threads_arm
