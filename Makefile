CXXFLAGS  := -Wall -Werror -g -std=c++17 -O2
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := client server

all: $(APPS)

client: client.o rmcclient.o rdmaclient.o rdmapeer.o
	$(CXX) -o $@ $^ ${LDLIBS}

server: server.o rmcserver.o rdmaserver.o rdmapeer.o
	$(CXX) -o $@ $^ ${LDLIBS}

.PHONY: clean
clean:
	rm -f *.o $(APPS)
