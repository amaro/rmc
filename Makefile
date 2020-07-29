CXXFLAGS  := -Wall -Werror -g -std=c++17 -O3
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := client nicserver hostserver

all: $(APPS)

client: client.o rdmaclient.o rdmapeer.o
	$(CXX) -o $@ $^ ${LDLIBS}

nicserver: nicserver.o rdmaserver.o rdmaclient.o rdmapeer.o
	$(CXX) -o $@ $^ ${LDLIBS}

hostserver: hostserver.o rdmaserver.o rdmapeer.o
	$(CXX) -o $@ $^ ${LDLIBS}

.PHONY: clean
clean:
	rm -f *.o $(APPS)
