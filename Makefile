B := build

CXXFLAGS := -Wall -Werror -g -std=c++17 -MMD -O3
LDLIBS := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS := client nicserver hostserver normc_client
APPS := $(addprefix $(B)/,$(APPS))
SRCS := $(wildcard *.cpp)
OBJS := $(patsubst %.cpp,$(B)/%.o,$(SRCS))
DEPS := ${OBJS:.o=.d}

all: $(APPS)

x86: $(B)/client $(B)/hostserver $(B)/normc_client

arm: $(B)/nicserver

$(B)/client: $(B)/client.o $(B)/onesidedclient.o $(B)/rdmaclient.o $(B)/rdmapeer.o

$(B)/nicserver: $(B)/nicserver.o $(B)/onesidedclient.o $(B)/rdmaserver.o $(B)/rdmaclient.o $(B)/rdmapeer.o

$(B)/hostserver: $(B)/hostserver.o $(B)/rdmaserver.o $(B)/rdmapeer.o

$(B)/normc_client: $(B)/normc_client.o $(B)/onesidedclient.o $(B)/rdmaclient.o $(B)/rdmapeer.o

$(APPS):
	$(CXX) -o $@ $^ ${LDLIBS}

$(B)/.:
	mkdir -p $@

.SECONDEXPANSION:

$(B)/%.o: %.cpp | $$(@D)/.
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(B)

.PHONY: all x86 arm clean

-include $(DEPS)
