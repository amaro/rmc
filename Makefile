B := build

CXXFLAGS := -Wall -Werror -g -std=c++17 -MMD -O3
LDLIBS := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS := client nicserver hostserver normc_client
APPS := $(addprefix $(B)/,$(APPS))
SRCS := $(wildcard *.cpp)
OBJS := $(patsubst %.cpp,$(B)/%.o,$(SRCS))
DEPS := ${OBJS:.o=.d}

.PHONY: all
all: $(APPS)

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

.PHONY: clean
clean:
	rm -rf $(B)

-include $(DEPS)
